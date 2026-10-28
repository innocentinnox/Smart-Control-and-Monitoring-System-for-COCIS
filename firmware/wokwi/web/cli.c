/* Serial console setup and shared helpers (see cli.h, cli_internal.h).
 *
 * The REPL task created here is the lowest-priority task in the system
 * (TASK_CLI_PRIO = 1), so typing commands never delays door decisions.
 */
#include "cli.h"

#include <stdio.h>

#include "auth.h"
#include "cli_internal.h"
#include "task_config.h"

void cli_register(const esp_console_cmd_t *cmds, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
}

bool cli_require_admin(void)
{
    if (auth_is_admin()) {
        return true;
    }
    printf("Admin session required. Run 'login', tap the admin card, then 'login <PIN>'.\n");
    return false;
}

int cli_usage(const char *usage)
{
    printf("usage: %s\n", usage);
    return 1;
}

void cli_start(void)
{
    /* REPL settings: prompt text, and the stack size / priority of the task
     * that esp_console creates (taken from the project's task table). */
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt         = "cocis>";
    repl_cfg.task_stack_size = TASK_CLI_STACK;
    repl_cfg.task_priority   = TASK_CLI_PRIO;

    /* Attach the REPL to the default UART (UART0, the USB serial monitor). */
    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl));

    /* Built-in "help" lists every registered command with its help text. */
    esp_console_register_help_command();
    cli_register_monitor_cmds();
    cli_register_admin_cmds();
    cli_register_sim_cmds();

    printf("\n=== CoCIS Smart Control & Monitoring ===\n"
           "Type 'help' for commands, 'status' for an overview.\n\n");
    /* Starts the REPL task; this function then returns and the console keeps
     * running in that task. */
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
