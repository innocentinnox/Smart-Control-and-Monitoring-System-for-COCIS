idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES common core_logic drivers bus storage services nodes cli esp_system)
