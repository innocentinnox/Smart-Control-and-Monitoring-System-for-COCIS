/* MFRC522 driver: register access, card exchange and the UID read sequence.
 * Register and command numbers are from the NXP MFRC522 datasheet (section 9);
 * the card commands (REQA, ANTICOLL, SELECT, HLTA) and CRC_A are from
 * ISO/IEC 14443-3 type A.
 */
#include "mfrc522.h"

#include <string.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ---------------- Chip registers ---------------- */
#define REG_COMMAND      0x01
#define REG_COM_IRQ      0x04   /* interrupt flags: what the last command did */
#define REG_ERROR        0x06
#define REG_FIFO_DATA    0x09   /* read/write here to pop/push the 64-byte FIFO */
#define REG_FIFO_LEVEL   0x0A   /* bytes in the FIFO; bit 7 = flush */
#define REG_CONTROL      0x0C   /* low 3 bits: valid bits in the last received byte */
#define REG_BIT_FRAMING  0x0D   /* bit 7 = start sending; low 3 bits = bits of the last byte to send */
#define REG_COLL         0x0E
#define REG_MODE         0x11
#define REG_TX_MODE      0x12
#define REG_RX_MODE      0x13
#define REG_TX_CONTROL   0x14   /* bits 0-1 drive the antenna pins */
#define REG_TX_ASK       0x15
#define REG_MOD_WIDTH    0x24
#define REG_T_MODE       0x2A   /* the chip's own timer, used as "no card answered" */
#define REG_T_PRESCALER  0x2B
#define REG_T_RELOAD_H   0x2C
#define REG_T_RELOAD_L   0x2D
#define REG_VERSION      0x37

/* CommandReg values */
#define CMD_IDLE         0x00
#define CMD_TRANSCEIVE   0x0C   /* send the FIFO to the card, then receive its answer */
#define CMD_SOFT_RESET   0x0F

/* ComIrqReg bits */
#define IRQ_TIMER        0x01   /* chip timer ran out: no answer */
#define IRQ_RX_IDLE      0x30   /* receive finished / command finished */

/* ErrorReg bits that make an answer unusable: buffer overflow, parity, protocol */
#define ERR_FATAL        0x13

/* ---------------- Card commands (ISO 14443-3 A) ---------------- */
#define PICC_REQA        0x26   /* wake cards that are idle, 7-bit frame */
#define PICC_WUPA        0x52   /* wake idle AND halted cards, 7-bit frame */
#define PICC_SEL_CL1     0x93   /* anticollision / select, cascade level 1..3 */
#define PICC_SEL_CL2     0x95
#define PICC_SEL_CL3     0x97
#define PICC_HLTA        0x50
#define CASCADE_TAG      0x88   /* "more UID bytes follow at the next level" */
#define SAK_INCOMPLETE   0x04

#define ANSWER_WAIT_MS   40     /* longer than the chip's 25 ms timer */

/* ================= SPI register access =================
 * Every SPI frame starts with an address byte: bits 6..1 = register,
 * bit 7 = 1 for read, 0 for write, bit 0 = 0. */

static esp_err_t write_regs(mfrc522_t *r, uint8_t reg, const uint8_t *data, size_t n)
{
    uint8_t tx[1 + 32];
    if (n > 32) {
        n = 32;                         /* longest frame we send is 9 bytes */
    }
    tx[0] = (reg << 1) & 0x7E;
    memcpy(&tx[1], data, n);
    spi_transaction_t t = { .length = 8 * (n + 1), .tx_buffer = tx };
    return spi_device_polling_transmit(r->spi, &t);
}

static void write_reg(mfrc522_t *r, uint8_t reg, uint8_t value)
{
    write_regs(r, reg, &value, 1);
}

static uint8_t read_reg(mfrc522_t *r, uint8_t reg)
{
    uint8_t tx[2] = { ((reg << 1) & 0x7E) | 0x80, 0 }, rx[2] = { 0 };
    spi_transaction_t t = { .length = 16, .tx_buffer = tx, .rx_buffer = rx };
    spi_device_polling_transmit(r->spi, &t);
    return rx[1];                       /* byte 0 arrives while the address is sent */
}

/* Read n bytes from one register (the FIFO): the address is repeated for each
 * byte, and every answer arrives one byte later. */
static void read_fifo(mfrc522_t *r, uint8_t *out, size_t n)
{
    uint8_t tx[1 + 32], rx[1 + 32];
    if (n > 32) {
        n = 32;
    }
    memset(tx, ((REG_FIFO_DATA << 1) & 0x7E) | 0x80, n);
    tx[n] = 0;
    spi_transaction_t t = { .length = 8 * (n + 1), .tx_buffer = tx, .rx_buffer = rx };
    spi_device_polling_transmit(r->spi, &t);
    memcpy(out, &rx[1], n);
}

static void set_bits(mfrc522_t *r, uint8_t reg, uint8_t mask)   { write_reg(r, reg, read_reg(r, reg) | mask); }
static void clear_bits(mfrc522_t *r, uint8_t reg, uint8_t mask) { write_reg(r, reg, read_reg(r, reg) & ~mask); }

/* ================= Card exchange ================= */

/* CRC_A of ISO 14443-3 (polynomial x^16 + x^12 + x^5 + 1, start 0x6363),
 * appended low byte first to SELECT and HLTA frames. */
static uint16_t crc_a(const uint8_t *data, size_t n)
{
    uint16_t crc = 0x6363;
    for (size_t i = 0; i < n; i++) {
        uint8_t ch = data[i] ^ (uint8_t)crc;
        ch ^= (uint8_t)(ch << 4);
        crc = (crc >> 8) ^ ((uint16_t)ch << 8) ^ ((uint16_t)ch << 3) ^ (ch >> 4);
    }
    return crc;
}

static size_t add_crc(uint8_t *frame, size_t n)
{
    uint16_t crc = crc_a(frame, n);
    frame[n]     = crc & 0xFF;
    frame[n + 1] = crc >> 8;
    return n + 2;
}

/* Send a frame to the card and collect its answer.
 * @param tx, tx_len   bytes to send
 * @param last_bits    bits to send of the last byte (0 = all 8; 7 for REQA/WUPA)
 * @param rx, rx_max   answer buffer and its size
 * @return number of answer bytes, 0 if no card answered, -1 on a radio error. */
static int transceive(mfrc522_t *r, const uint8_t *tx, size_t tx_len, uint8_t last_bits,
                      uint8_t *rx, size_t rx_max)
{
    write_reg(r, REG_COMMAND, CMD_IDLE);            /* stop anything still running */
    write_reg(r, REG_COM_IRQ, 0x7F);                /* clear all interrupt flags */
    write_reg(r, REG_FIFO_LEVEL, 0x80);             /* empty the FIFO */
    write_regs(r, REG_FIFO_DATA, tx, tx_len);
    write_reg(r, REG_BIT_FRAMING, last_bits);
    write_reg(r, REG_COMMAND, CMD_TRANSCEIVE);
    set_bits(r, REG_BIT_FRAMING, 0x80);             /* StartSend */

    /* Wait for "answer received" or the chip's 25 ms "no answer" timer.
     * The limit is measured in real time (esp_timer, microseconds), and each
     * pause is one scheduler tick, so the wait is the same whatever the
     * FreeRTOS tick rate (1 ms here, 10 ms in a default ESP-IDF build). */
    uint8_t irq = 0;
    int64_t give_up_us = esp_timer_get_time() + ANSWER_WAIT_MS * 1000;
    while (1) {
        irq = read_reg(r, REG_COM_IRQ);
        if ((irq & (IRQ_RX_IDLE | IRQ_TIMER)) || esp_timer_get_time() > give_up_us) {
            break;
        }
        vTaskDelay(1);
    }
    clear_bits(r, REG_BIT_FRAMING, 0x80);
    if (!(irq & IRQ_RX_IDLE)) {
        return 0;                                   /* timer fired or we gave up: no card */
    }
    if (read_reg(r, REG_ERROR) & ERR_FATAL) {
        return -1;
    }
    size_t n = read_reg(r, REG_FIFO_LEVEL);
    if (n > rx_max) {
        n = rx_max;
    }
    read_fifo(r, rx, n);
    return (int)n;
}

/* REQA, and WUPA if nothing answered (a card left in the field after HLTA only
 * answers WUPA). An ISO 14443 A card answers with a 2-byte ATQA. */
static bool card_present(mfrc522_t *r)
{
    uint8_t atqa[2];
    clear_bits(r, REG_COLL, 0x80);                  /* clear bits received after a collision */
    uint8_t req = PICC_REQA;
    if (transceive(r, &req, 1, 7, atqa, sizeof(atqa)) == 2) {
        return true;
    }
    req = PICC_WUPA;
    return transceive(r, &req, 1, 7, atqa, sizeof(atqa)) == 2;
}

/* One cascade level: ANTICOLL gets 4 UID bytes + BCC (their XOR), SELECT
 * confirms them and returns SAK.
 * @param sel  PICC_SEL_CL1/2/3
 * @param out  the 4 bytes of this level
 * @param sak  out: the card's SAK byte
 * @return false on no answer, bad length or bad check byte. */
static bool cascade_level(mfrc522_t *r, uint8_t sel, uint8_t out[4], uint8_t *sak)
{
    uint8_t frame[9] = { sel, 0x20 }, ans[5];       /* 0x20 = "I send 2 bytes, you send the rest" */
    if (transceive(r, frame, 2, 0, ans, sizeof(ans)) != 5 ||
        (ans[0] ^ ans[1] ^ ans[2] ^ ans[3]) != ans[4]) {
        return false;
    }
    frame[1] = 0x70;                                /* 0x70 = full 7-byte SELECT frame */
    memcpy(&frame[2], ans, 5);
    uint8_t sak_crc[3];
    if (transceive(r, frame, add_crc(frame, 7), 0, sak_crc, sizeof(sak_crc)) < 1) {
        return false;
    }
    memcpy(out, ans, 4);
    *sak = sak_crc[0];
    return true;
}

static void halt(mfrc522_t *r)
{
    uint8_t frame[4] = { PICC_HLTA, 0x00 }, ans[2];
    transceive(r, frame, add_crc(frame, 2), 0, ans, sizeof(ans));   /* no answer = success */
}

/* ================= Public API ================= */

esp_err_t mfrc522_init(mfrc522_t *r, spi_host_device_t host, int cs_gpio, int clock_hz)
{
    spi_device_interface_config_t dev = {
        .mode           = 0,                        /* MFRC522 uses SPI mode 0 */
        .clock_speed_hz = clock_hz,
        .spics_io_num   = cs_gpio,
        .queue_size     = 1,
    };
    if (!r->spi) {                                  /* first call: attach to the bus once */
        esp_err_t err = spi_bus_add_device(host, &dev, &r->spi);
        if (err != ESP_OK) {
            return err;
        }
    }

    write_reg(r, REG_COMMAND, CMD_SOFT_RESET);
    vTaskDelay(pdMS_TO_TICKS(50));                  /* oscillator start-up */
    r->version = read_reg(r, REG_VERSION);
    if (r->version == 0x00 || r->version == 0xFF) {
        return ESP_ERR_NOT_FOUND;                   /* nothing driving MISO */
    }

    write_reg(r, REG_TX_MODE, 0x00);                /* 106 kbit/s, no CRC added by the chip */
    write_reg(r, REG_RX_MODE, 0x00);
    write_reg(r, REG_MOD_WIDTH, 0x26);
    /* Timer: 13.56 MHz / (2 * 0xA9 + 1) = 40 kHz ticks, reload 1000 -> 25 ms */
    write_reg(r, REG_T_MODE, 0x80);                 /* start automatically after sending */
    write_reg(r, REG_T_PRESCALER, 0xA9);
    write_reg(r, REG_T_RELOAD_H, 0x03);
    write_reg(r, REG_T_RELOAD_L, 0xE8);
    write_reg(r, REG_TX_ASK, 0x40);                 /* 100 % ASK modulation (type A) */
    write_reg(r, REG_MODE, 0x3D);                   /* CRC preset 0x6363 (type A) */
    set_bits(r, REG_TX_CONTROL, 0x03);              /* antenna on */
    return ESP_OK;
}

bool mfrc522_read_uid(mfrc522_t *r, uint8_t uid[MFRC522_UID_MAX], uint8_t *len)
{
    if (!card_present(r)) {
        return false;
    }
    static const uint8_t SEL[3] = { PICC_SEL_CL1, PICC_SEL_CL2, PICC_SEL_CL3 };
    uint8_t n = 0;
    for (int level = 0; level < 3; level++) {
        uint8_t part[4], sak;
        if (!cascade_level(r, SEL[level], part, &sak)) {
            return false;
        }
        bool more = sak & SAK_INCOMPLETE;
        if (more && part[0] == CASCADE_TAG) {       /* first byte is a marker, not UID */
            memcpy(&uid[n], &part[1], 3);
            n += 3;
        } else {
            memcpy(&uid[n], part, 4);
            n += 4;
        }
        if (!more) {
            halt(r);
            *len = n;
            return true;
        }
    }
    return false;
}
