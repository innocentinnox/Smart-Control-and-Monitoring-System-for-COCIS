#include "rfid.h"

#include "board_pins.h"
#include "bus.h"
#include "buttons.h"
#include "users.h"

static const gpio_num_t s_card_pins[DEMO_CARD_COUNT] = {
    PIN_CARD_BTN_1, PIN_CARD_BTN_2, PIN_CARD_BTN_3, PIN_CARD_BTN_4,
};

bool rfid_inject(card_uid_t uid, door_side_t side)
{
    bus_msg_t m = { .type = MSG_CARD_SCAN, .scan = { .uid = uid, .side = side } };
    return bus_send(NODE_HUB, NODE_HUB, &m, pdMS_TO_TICKS(50));
}

door_side_t rfid_reader_side(void)
{
    return gpio_get_level(PIN_READER_SELECT) ? SIDE_OUT : SIDE_IN;
}

static void on_card_button(size_t index)
{
    rfid_inject(users_demo_card((int)index), rfid_reader_side());
}

void rfid_init(void)
{
    gpio_config_t sel = {
        .pin_bit_mask = 1ULL << PIN_READER_SELECT,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&sel));
    buttons_add_group(s_card_pins, DEMO_CARD_COUNT, on_card_button);
}
