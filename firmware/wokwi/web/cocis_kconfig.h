/* The project's tunable settings (CONFIG_COCIS_*), with fallbacks.
 *
 * Normally these come from main/Kconfig.projbuild via menuconfig and arrive
 * in sdkconfig.h. A build that has no Kconfig for this project (the
 * wokwi.com online ESP-IDF builder, which only compiles the source files)
 * gets the same default values from here instead, so the firmware behaves
 * identically. Keep the numbers below equal to the Kconfig defaults.
 * Include this file instead of sdkconfig.h wherever a CONFIG_COCIS_ value
 * is used.
 */
#pragma once

#include "sdkconfig.h"

#ifndef CONFIG_COCIS_OPEN_HOUR
#define CONFIG_COCIS_OPEN_HOUR       7       /* building opens, hour 0..23 */
#endif
#ifndef CONFIG_COCIS_CLOSE_HOUR
#define CONFIG_COCIS_CLOSE_HOUR      22      /* building closes, hour 0..23 */
#endif
#ifndef CONFIG_COCIS_DOOR_OPEN_MS
#define CONFIG_COCIS_DOOR_OPEN_MS    3000    /* door stays open, ms */
#endif
#ifndef CONFIG_COCIS_ESCORT_S
#define CONFIG_COCIS_ESCORT_S        60      /* after-hours escort window, s */
#endif
#ifndef CONFIG_COCIS_ADMIN_SESSION_S
#define CONFIG_COCIS_ADMIN_SESSION_S 300     /* admin session length, s */
#endif
#ifndef CONFIG_COCIS_ROOM_GRACE_S
#define CONFIG_COCIS_ROOM_GRACE_S    30      /* no motion this long = vacant, s */
#endif
#ifndef CONFIG_COCIS_ROOM_WARN_S
#define CONFIG_COCIS_ROOM_WARN_S     10      /* dim warning before off, s */
#endif
#ifndef CONFIG_COCIS_DARK_LUX
#define CONFIG_COCIS_DARK_LUX        150     /* at or below: dark, lux */
#endif
#ifndef CONFIG_COCIS_BRIGHT_LUX
#define CONFIG_COCIS_BRIGHT_LUX      400     /* at or above: daylight, lux */
#endif
#ifndef CONFIG_COCIS_LAMP_WATTS
#define CONFIG_COCIS_LAMP_WATTS      360     /* lamp bank per room, W */
#endif
#ifndef CONFIG_COCIS_NODE_TIMEOUT_S
#define CONFIG_COCIS_NODE_TIMEOUT_S  15      /* silent room = offline, s */
#endif
