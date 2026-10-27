# core_logic: pure decision rules (no ESP-IDF APIs), unit-tested on the PC
# by firmware/test/host.
idf_component_register(SRCS "access_policy.c" "room_logic.c" "auth_fsm.c"
                       INCLUDE_DIRS "include"
                       REQUIRES common)
