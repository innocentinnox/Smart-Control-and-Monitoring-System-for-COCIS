/* RFID reader front-end. Whatever the reader hardware is, a scan ends up as
 * one MSG_CARD_SCAN to the hub, so the access logic never knows the source.
 *
 * Backend in this build: Wokwi has no MFRC522 part, so four push-buttons are
 * the four demo cards and a slide switch picks the entry or exit reader.
 * The serial command "tap <uid> in|out" injects any UID the same way.
 */
#pragma once

#include "cocis_types.h"

/* Configure the reader-select switch pin and register the four card
 * buttons with the buttons driver. Call once at boot (from hub_start()). */
void rfid_init(void);

/* Pretend a card was held at a reader: sends MSG_CARD_SCAN to the hub inbox.
 * @param uid   card UID (any 32-bit value, e.g. from the CLI "tap" command)
 * @param side  which reader: SIDE_IN (entry) or SIDE_OUT (exit)
 * @return false if the hub inbox stayed full for 50 ms (scan lost).
 * Safe from any task (button task, CLI task). */
bool rfid_inject(card_uid_t uid, door_side_t side);

/* Which reader the slide switch currently selects.
 * @return SIDE_IN or SIDE_OUT, read live from the switch GPIO. */
door_side_t rfid_reader_side(void);
