/* Serial management menu (UART0 REPL built on esp_console).
 * Type "help" for the command list; see docs/report.pdf (appendix A) for every command.
 *
 * esp_console is ESP-IDF's command-line framework: it reads a line from the
 * serial port, splits it into words (argc/argv like a C main()), looks up
 * the first word in a table of registered commands and calls its handler.
 * A REPL (Read-Eval-Print Loop) runs this in its own FreeRTOS task.
 *
 * Commands are split over three files: cmd_monitor.c (read-only, anyone),
 * cmd_admin.c (changes, need an admin login) and cmd_sim.c (demo aids).
 */
#pragma once

/* Create the UART REPL task, register all commands and start it.
 * Call once at the end of boot, after every service is initialised.
 * Aborts (ESP_ERROR_CHECK) if the console cannot be created. */
void cli_start(void);
