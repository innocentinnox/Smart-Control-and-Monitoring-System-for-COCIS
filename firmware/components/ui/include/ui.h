/* Entrance user interface: the 20x4 LCD, buzzer and "granted" LED.
 *
 * One ui task owns all three devices (no other task touches them), so no
 * locking is needed around the LCD. Other tasks send requests through a
 * queue. The idle screen is drawn from a status snapshot supplied by a
 * provider callback, so the ui knows nothing about the hub's internals.
 *
 *   row 0  CoCIS 09:15:02  OPEN      <- always
 *   row 1  Hours 07:00-22:00         <- or a 2-line message for a few seconds
 *   row 2  Inside: 3       ADMIN
 *   row 3  R1 100% occ  R2   0% ---  <- always
 */
#pragma once

#include <stddef.h>
#include "cocis_types.h"

typedef struct {
    bool   open;                /* within opening hours          */
    bool   lockdown;
    bool   emergency;
    bool   admin;               /* admin session active          */
    char   hours[16];           /* "07:00-22:00"                 */
    size_t inside;
    struct {
        bool    online;
        bool    occupied;
        uint8_t level;
    } rooms[ROOM_COUNT];
} ui_status_t;

typedef void (*ui_status_fn)(ui_status_t *out);

typedef enum {
    UI_SOUND_GRANTED,           /* short high beep + LED */
    UI_SOUND_DENIED,            /* three low beeps       */
    UI_SOUND_KEY,               /* keypad click          */
    UI_SOUND_ALARM,             /* break-glass siren     */
    UI_SOUND_INFO,              /* two-tone notice       */
} ui_sound_t;

void ui_init(ui_status_fn provider);

/* Show two lines (rows 1-2) for ms milliseconds, then return to idle. */
void ui_message(const char *line1, const char *line2, uint32_t ms);

void ui_feedback(ui_sound_t sound);
