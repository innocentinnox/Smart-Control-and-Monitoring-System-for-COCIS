/* Entrance user interface: the 20x4 LCD, buzzer and "granted" LED.
 *
 * One ui task owns all three devices (no other task touches them), so no
 * locking is needed around the LCD. Other tasks send requests through a
 * queue. The idle screen is drawn from a status snapshot supplied by a
 * provider callback, so the ui knows nothing about the hub's internals.
 *
 *   row 0  CoCIS 09:15:02  OPEN      <- always (OPEN / SHUT / LOCK / EMRG)
 *   row 1  Hours 07:00-22:00         <- or a 2-line message for a few seconds
 *   row 2  Inside: 3      ADMIN      <- people in the building, admin session
 *   row 3  R1 100% P-R2   0% -S      <- always (see room_cell() in ui.c)
 *
 * Because ui_message() and ui_feedback() only drop a request into the queue,
 * callers (hub, room nodes, CLI) never wait for the slow LCD or for
 * a beep to finish.
 */
#pragma once

#include <stddef.h>
#include "cocis_types.h"

/* Everything the idle screen shows, filled in by the provider callback */
typedef struct {
    bool   open;                /* within opening hours          */
    bool   lockdown;            /* lockdown mode switched on       */
    bool   emergency;           /* break-glass emergency active  */
    bool   admin;               /* admin session active          */
    char   hours[16];           /* "07:00-22:00"                 */
    size_t inside;              /* number of people inside       */
    struct {
        bool    online;         /* room node is still reporting  */
        bool    occupied;       /* people detected in the room   */
        bool    daylight;       /* enough sunlight in the room   */
        uint8_t level;          /* lamp brightness 0..100 %      */
    } rooms[ROOM_COUNT];
} ui_status_t;

/* Status provider: fill *out with the current system state (out is zeroed
 * before the call). Called by the ui task about every 500 ms, so it must be
 * quick and thread-safe with respect to the caller's own data. */
typedef void (*ui_status_fn)(ui_status_t *out);

/* Sound patterns for ui_feedback() */
typedef enum {
    UI_SOUND_GRANTED,           /* short high beep + LED */
    UI_SOUND_DENIED,            /* three low beeps       */
    UI_SOUND_ALARM,             /* break-glass siren     */
    UI_SOUND_INFO,              /* two-tone notice       */
} ui_sound_t;

/* Set up the LED, buzzer and LCD and start the ui task.
 * @param provider  callback that supplies the idle-screen status (required,
 *                  never NULL: it is called on every refresh)
 * If no LCD answers the ui keeps running "headless" (sounds and LED still
 * work). Call once at boot, after i2c_bus_init() and the clock service.
 */
void ui_init(ui_status_fn provider);

/* Show two lines (rows 1-2) for ms milliseconds, then return to idle.
 * @param line1  text for row 1 (cut to 20 chars; NULL = empty)
 * @param line2  text for row 2 (cut to 20 chars; NULL = empty)
 * @param ms     how long the message stays, in milliseconds
 * A newer message replaces the current one. Never blocks: if the ui queue
 * is full the message is silently dropped. Safe from any task (not ISR).
 */
void ui_message(const char *line1, const char *line2, uint32_t ms);

/* Play a sound pattern (and for UI_SOUND_GRANTED light the green LED for
 * 2 s).
 * @param sound  which pattern to play
 * Never blocks: the ui task plays it; dropped if the ui queue is full.
 * Safe from any task (not ISR).
 */
void ui_feedback(ui_sound_t sound);
