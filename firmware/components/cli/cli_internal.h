/* Shared helpers for the command files. */
#pragma once

#include <stddef.h>
#include "esp_console.h"

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

void cli_register(const esp_console_cmd_t *cmds, size_t count);

/* Prints a hint and returns false if no admin session is active. */
bool cli_require_admin(void);

/* Prints "usage: <usage>" and returns 1 (the command's exit code). */
int cli_usage(const char *usage);

void cli_register_monitor_cmds(void);   /* status who rooms users log      */
void cli_register_admin_cmds(void);     /* login schedule user light time  */
void cli_register_sim_cmds(void);       /* tap node                        */
