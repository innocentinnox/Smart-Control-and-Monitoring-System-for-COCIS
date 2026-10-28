# nodes: the three terminals (entrance hub + two room nodes) and the
# entrance's doors and RFID readers.
idf_component_register(SRCS "hub.c" "hub_access.c" "door.c" "rfid.c" "room_node.c"
                       INCLUDE_DIRS "include"
                       REQUIRES common bus
                       PRIV_REQUIRES core_logic drivers services storage ui esp_timer)
