# common: shared types, pin map, task table and crypto helper.
idf_component_register(SRCS "cocis_types.c" "cocis_crypto.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_gpio esp_driver_ledc
                       PRIV_REQUIRES mbedtls)
