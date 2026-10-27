# storage: SD card gatekeeper task + tamper-evident (hash-chained) event log.
idf_component_register(SRCS "storage.c"
                       INCLUDE_DIRS "include"
                       REQUIRES common
                       PRIV_REQUIRES fatfs sdmmc esp_driver_sdspi esp_driver_spi vfs)
