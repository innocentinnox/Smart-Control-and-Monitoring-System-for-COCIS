/* Shared helpers for the command files (private to the cli component).
 *
 * Every command is a handler `int cmd_x(int argc, char **argv)`: argv[0] is
 * the command name, argv[1..] the words typed after it. Returning 0 means
 * success, non-zero is reported as an error by esp_console.
 *
 * Note: ESP-IDF examples usually parse arguments with argtable3 (a library
 * that declares options like "--hour <n>" and parses them). This project
 * does not use it: the commands are short positional words, so each handler
 * checks argc/argv by hand and prints a usage line on mistakes.
 */
#pragma once

#include <stddef.h>
#include "esp_console.h"

/* Number of elements in a fixed-size array. */
#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

/* Register a table of commands with esp_console.
 * @param cmds   array of {command name, help text, handler}; must stay valid
 *               (the command files use static const tables).
 * @param count  number of entries. Aborts on a registration error. */
void cli_register(const esp_console_cmd_t *cmds, size_t count);

/* Prints a hint and returns false if no admin session is active.
 * Called by every command that changes the system, so that changes are only
 * possible after the two-factor login, and the change can be logged with
 * the admin's card UID. */
bool cli_require_admin(void);

/* Prints "usage: <usage>" and returns 1 (the command's exit code).
 * Lets handlers write `return cli_usage("...");`. */
int cli_usage(const char *usage);

/* Each command file registers its own table (called from cli_start()). */
void cli_register_monitor_cmds(void);   /* status who rooms users log      */
void cli_register_admin_cmds(void);     /* login schedule user light time  */
void cli_register_sim_cmds(void);       /* tap node                        */
