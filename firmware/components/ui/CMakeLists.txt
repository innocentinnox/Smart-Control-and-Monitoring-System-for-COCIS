# ui: LCD + buzzer + LED, owned by one task.
idf_component_register(SRCS "ui.c"
                       INCLUDE_DIRS "include"
                       REQUIRES common
                       PRIV_REQUIRES drivers services)
