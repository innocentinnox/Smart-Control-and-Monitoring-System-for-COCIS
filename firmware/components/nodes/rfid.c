/* RFID front-end, simulated backend (see rfid.h).
 *
 * Card buttons 1..4 stand for demo cards 1..4; the slide switch says which
 * reader the card is held at. A press becomes exactly the message a real
 * reader driver would send (MSG_CARD_SCAN), so the hub cannot tell the
 * difference. Swapping in a real MFRC522 only changes this file.
 */
#include "rfid.h"

#include "board_pins.h"
#include "bus.h"
#include "buttons.h"
#include "users.h"

/* Button index i (0..3) -> demo card i. Must stay static: the buttons
 * driver keeps a pointer to this array. */
static const gpio_num_t s_card_pins[DEMO_CARD_COUNT] = {
    PIN_CARD_BTN_1, PIN_CARD_BTN_2, PIN_CARD_BTN_3, PIN_CARD_BTN_4,
};

bool rfid_inject(card_uid_t uid, door_side_t side)
{
    bus_msg_t m = { .type = MSG_CARD_SCAN, .scan = { .uid = uid, .side = side } };
    /* The reader is part of the hub terminal, so it sends as NODE_HUB to the
     * hub's own inbox. pdMS_TO_TICKS() converts ms to FreeRTOS ticks (the
     * scheduler's time unit); wait at most 50 ms if the inbox is full. */
    return bus_send(NODE_HUB, NODE_HUB, &m, pdMS_TO_TICKS(50));
}

door_side_t rfid_reader_side(void)
{
    /* Switch open (pulled up, level 1) = exit reader; closed to GND = entry. */
    return gpio_get_level(PIN_READER_SELECT) ? SIDE_OUT : SIDE_IN;
}

/* Buttons callback (runs in the buttons task after debouncing).
 * @param index  which card button was pressed, 0..DEMO_CARD_COUNT-1 */
static void on_card_button(size_t index)
{
    rfid_inject(users_demo_card((int)index), rfid_reader_side());
}

void rfid_init(void)
{
    /* Reader-select slide switch: plain input with the internal pull-up. */
    gpio_config_t sel = {
        .pin_bit_mask = 1ULL << PIN_READER_SELECT,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&sel));
    buttons_add_group(s_card_pins, DEMO_CARD_COUNT, on_card_button);
}
