# services: shared, persistent system state (users, config, occupancy,
# admin auth, clock). Each module guards its data with its own mutex.
idf_component_register(SRCS "nvs_store.c" "users.c" "config.c" "occupancy.c" "auth.c" "clock.c"
                       INCLUDE_DIRS "include"
                       REQUIRES common core_logic storage
                       PRIV_REQUIRES nvs_flash drivers esp_timer)
