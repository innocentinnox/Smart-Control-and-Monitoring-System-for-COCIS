# cli: serial management menu (esp_console REPL on UART0).
idf_component_register(SRCS "cli.c" "cmd_monitor.c" "cmd_admin.c" "cmd_sim.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES console common core_logic bus services storage nodes esp_timer)
