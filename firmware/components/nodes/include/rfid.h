/* RFID reader front-end. Whatever the reader hardware is, a scan ends up as
 * one MSG_CARD_SCAN to the hub, so the access logic never knows the source.
 *
 * Backend in this build: Wokwi has no MFRC522 part, so four push-buttons are
 * the four demo cards and a slide switch picks the entry or exit reader.
 * The serial command "tap <uid> in|out" injects any UID the same way.
 */
#pragma once

#include "cocis_types.h"

void rfid_init(void);
bool rfid_inject(card_uid_t uid, door_side_t side);
