/* RFID front end (see rfid.h): owns the readers' SPI bus and both readers.
 *
 * Card UID -> card_uid_t: a 4-byte UID is read first byte first, so the Wokwi
 * Blue card 01:02:03:04 becomes 0x01020304, printed "01020304". A 7-byte UID
 * keeps its last 4 bytes (the serial number part; the first byte only names
 * the chip maker).
 *
 * Presentation logic, per reader: a card counts as a new scan when it was not
 * seen at that reader during the last REPRESENT_MS. A card held on the reader
 * keeps "being seen", so it is reported once; take it away and present it
 * again and it is a new scan (a person coming back through the door).
 */
#include "rfid.h"

#include "board_pins.h"
#include "bus.h"
#include "clock.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mfrc522.h"
#include "storage.h"
#include "task_config.h"

static const char *TAG = "rfid";

#define RFID_HOST       SPI3_HOST   /* SPI2 belongs to the SD card */
#define POLL_MS         100         /* how often each reader is asked for a card */
#define REPRESENT_MS    1500        /* gap after which the same card counts again */
#define RETRY_MS        5000        /* how often a dead reader is re-initialised */

typedef struct {
    door_side_t   side;
    int           cs_gpio;
    mfrc522_t     dev;
    volatile bool ok;               /* chip answered at the last init; read by other tasks */
    bool          alerted;          /* RFID_FAIL already logged for this outage */
    uint32_t      next_retry_ms;
    card_uid_t    last_uid;         /* last card seen here ... */
    uint32_t      last_seen_ms;     /* ... and when */
} reader_t;

static reader_t s_readers[2] = {
    { .side = SIDE_IN,  .cs_gpio = PIN_RFID_IN_CS  },
    { .side = SIDE_OUT, .cs_gpio = PIN_RFID_OUT_CS },
};

bool rfid_inject(card_uid_t uid, door_side_t side)
{
    bus_msg_t m = { .type = MSG_CARD_SCAN, .scan = { .uid = uid, .side = side } };
    /* The readers are part of the hub terminal, so a scan is sent as NODE_HUB
     * to the hub's own inbox; wait at most 50 ms if the inbox is full. */
    return bus_send(NODE_HUB, NODE_HUB, &m, pdMS_TO_TICKS(50));
}

bool rfid_reader_ok(door_side_t side)
{
    return s_readers[side == SIDE_OUT].ok;
}

/* UID bytes -> 32-bit card id (see the file header). */
static card_uid_t uid_to_card(const uint8_t *uid, uint8_t len)
{
    const uint8_t *b = &uid[len - 4];
    return ((card_uid_t)b[0] << 24) | ((card_uid_t)b[1] << 16) | ((card_uid_t)b[2] << 8) | b[3];
}

/* (Re)initialise one reader; logs the first failure of an outage only.
 * The SPI device is added once; later retries just reset the chip again. */
static void reader_start(reader_t *r)
{
    esp_err_t err = mfrc522_init(&r->dev, RFID_HOST, r->cs_gpio, RFID_SPI_HZ);
    r->ok = err == ESP_OK;
    if (r->ok) {
        ESP_LOGI(TAG, "%s reader ready (MFRC522 version 0x%02X)", side_str(r->side), r->dev.version);
        r->alerted = false;
    } else if (!r->alerted) {
        ESP_LOGW(TAG, "%s reader not answering (%s)", side_str(r->side), esp_err_to_name(err));
        cocis_log(LOG_ALERT, "code=RFID_FAIL detail=%s", side_str(r->side));
        r->alerted = true;
    }
    r->next_retry_ms = clock_ms() + RETRY_MS;
}

/* Poll one reader once and report a newly presented card. */
static void reader_poll(reader_t *r)
{
    uint8_t uid[MFRC522_UID_MAX], len;
    if (!mfrc522_read_uid(&r->dev, uid, &len) || len < 4) {
        return;
    }
    card_uid_t card = uid_to_card(uid, len);
    uint32_t now = clock_ms();
    bool same = card == r->last_uid && now - r->last_seen_ms < REPRESENT_MS;
    r->last_uid     = card;
    r->last_seen_ms = now;
    if (!same) {
        rfid_inject(card, r->side);
    }
}

/* The rfid task: polls both readers forever, retrying any reader that failed.
 * @param arg  unused */
static void rfid_task(void *arg)
{
    while (1) {
        for (int i = 0; i < 2; i++) {
            reader_t *r = &s_readers[i];
            if (r->ok) {
                reader_poll(r);
            } else if (time_reached_ms(clock_ms(), r->next_retry_ms)) {
                reader_start(r);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

void rfid_init(void)
{
    /* Hardware reset of both readers: hold RST low, then release. */
    gpio_config_t rst = { .pin_bit_mask = 1ULL << PIN_RFID_RST, .mode = GPIO_MODE_OUTPUT };
    ESP_ERROR_CHECK(gpio_config(&rst));
    gpio_set_level(PIN_RFID_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_RFID_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    /* The readers' own bus. No DMA: frames are at most 10 bytes. */
    spi_bus_config_t bus = {
        .sclk_io_num = PIN_RFID_SCK,
        .mosi_io_num = PIN_RFID_MOSI,
        .miso_io_num = PIN_RFID_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(RFID_HOST, &bus, SPI_DMA_DISABLED));

    for (int i = 0; i < 2; i++) {
        reader_start(&s_readers[i]);
    }
    xTaskCreate(rfid_task, "rfid", TASK_RFID_STACK, NULL, TASK_RFID_PRIO, NULL);
}
