/* MFRC522 RFID reader (13.56 MHz, ISO/IEC 14443 A cards) over SPI.
 *
 * The reader is a radio front end: the ESP32 writes a few bytes into its FIFO,
 * tells it to transmit them to the card ("Transceive"), and reads the card's
 * answer back from the FIFO. Reading a card's UID takes four exchanges:
 *   REQA/WUPA   "is a card there?"            -> ATQA (2 bytes)
 *   ANTICOLL    "send me your UID"             -> 4 UID bytes + check byte
 *   SELECT      "you, with this UID, wake up"  -> SAK (says if more UID follows)
 *   HLTA        "go to sleep"                  -> no answer
 * Cards with 7-byte UIDs answer in two "cascade levels" of 3 + 4 bytes.
 *
 * The driver only knows the chip; which reader is which door and what a UID
 * means is decided in nodes/rfid.c. It is not thread-safe per reader: one
 * task (the rfid task) owns all readers.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/spi_master.h"
#include "esp_err.h"

#define MFRC522_UID_MAX 10      /* longest ISO 14443 A UID (triple size), bytes */

/* One reader on an SPI bus. */
typedef struct {
    spi_device_handle_t spi;    /* SPI device: the bus + this reader's chip-select */
    uint8_t             version;/* VersionReg read at init: 0x91/0x92 on real chips */
} mfrc522_t;

/* Attach a reader to an SPI bus that is already initialised
 * (spi_bus_initialize()) and bring the chip up: soft reset, 25 ms card
 * timeout, 100 % ASK modulation, antenna on.
 * @param r         reader handle to fill
 * @param host      SPI bus the reader is wired to, e.g. SPI3_HOST
 * @param cs_gpio   the reader's chip-select pin (labelled SDA on the module)
 * @param clock_hz  SPI clock, at most 10 MHz for the MFRC522
 * @return ESP_OK; ESP_ERR_NOT_FOUND if the chip does not answer (VersionReg
 *         reads 0x00 or 0xFF: unwired or unpowered); or an SPI driver error.
 * Calling it again on the same handle (r must start zeroed) only resets and
 * reconfigures the chip, so it can be used to retry a reader that failed.
 * Call from the one task that owns the reader. */
esp_err_t mfrc522_init(mfrc522_t *r, spi_host_device_t host, int cs_gpio, int clock_hz);

/* Look for one card in the field and read its UID.
 * @param r     reader from mfrc522_init()
 * @param uid   out: UID bytes, first byte first (MFRC522_UID_MAX room)
 * @param len   out: UID length, 4 or 7 (or 10)
 * @return true if a card answered and its UID was read and checked.
 * The card is put to sleep (HLTA) afterwards, so a card that stays in the
 * field answers again only after it is woken (WUPA) or presented again.
 * Takes about 2-30 ms (30 ms when no card answers). */
bool mfrc522_read_uid(mfrc522_t *r, uint8_t uid[MFRC522_UID_MAX], uint8_t *len);
