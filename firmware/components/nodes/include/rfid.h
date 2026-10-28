/* RFID front end of the entrance: the entry reader and the exit reader.
 *
 * Two MFRC522 readers (drivers/mfrc522.h) sit on their own SPI bus. The rfid
 * task polls both every 100 ms; when a card arrives at a reader it becomes
 * exactly one MSG_CARD_SCAN (UID + side) in the hub inbox, so the access
 * logic only ever sees "this card, at this door". A card held still on a
 * reader is reported once, not every poll.
 *
 * The serial command "tap <uid> in|out" injects a scan the same way, for
 * testing any UID.
 */
#pragma once

#include "cocis_types.h"

/* Reset both readers, start their SPI bus, check each chip answers, and
 * start the rfid task. A reader that does not answer is logged as
 * ALERT code=RFID_FAIL and retried every 5 s, so the system still boots.
 * Call once at boot (from hub_start()). */
void rfid_init(void);

/* Report a card scan to the hub: sends MSG_CARD_SCAN to the hub inbox.
 * @param uid   card UID (any 32-bit value)
 * @param side  which reader: SIDE_IN (entry) or SIDE_OUT (exit)
 * @return false if the hub inbox stayed full for 50 ms (scan lost).
 * Safe from any task (rfid task, CLI task). */
bool rfid_inject(card_uid_t uid, door_side_t side);

/* Whether a reader answered at its last check.
 * @param side  SIDE_IN (entry reader) or SIDE_OUT (exit reader)
 * @return true if that reader is working. Safe from any task. */
bool rfid_reader_ok(door_side_t side);
