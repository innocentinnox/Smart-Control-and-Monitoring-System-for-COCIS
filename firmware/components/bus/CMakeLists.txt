# bus: message passing between the three terminals.
idf_component_register(SRCS "bus.c"
                       INCLUDE_DIRS "include"
                       REQUIRES common freertos)
