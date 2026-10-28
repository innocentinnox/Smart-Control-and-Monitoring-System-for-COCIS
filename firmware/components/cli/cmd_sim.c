/* Simulation / test aids: drive the system from the console without
 * clicking parts in Wokwi (handy for repeatable demo scripts).
 *
 * These need no admin login: they only imitate things a person could do
 * physically (tap a card, unplug a room terminal).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli_internal.h"
#include "rfid.h"
#include "room_node.h"

/* "tap <UID> in|out": pretend card UID was held at the entry (in) or exit
 * (out) reader. It goes through exactly the same path as a real scan
 * (MSG_CARD_SCAN to the hub), so the full access logic runs. Any UID works,
 * including unregistered ones (to demo "unknown card"). */
static int cmd_tap(int argc, char **argv)
{
    card_uid_t uid;
    door_side_t side;
    if (argc < 3 || !card_uid_parse(argv[1], &uid) || !side_parse(argv[2], &side)) {
        return cli_usage("tap <UID> in|out   e.g. tap C0FFEE99 in");
    }
    /* Fails (exit code 1) only if the hub inbox stayed full. */
    return rfid_inject(uid, side) ? 0 : 1;
}

/* "node <room> offline|online": make a room node stop (or resume) talking to
 * the hub, to show that the hub detects the missing heartbeat and that the
 * room keeps controlling its lights on its own. */
static int cmd_node(int argc, char **argv)
{
    int room = argc > 1 ? atoi(argv[1]) : 0;
    /* strcmp() returns 0 on a match: reject anything that is neither word. */
    if (argc < 3 || room < 1 || room > ROOM_COUNT ||
        (strcmp(argv[2], "offline") && strcmp(argv[2], "online"))) {
        return cli_usage("node <1|2> offline|online   (simulate a link failure)");
    }
    room_node_set_link(room, strcmp(argv[2], "online") == 0);
    printf("Room %d link %s.\n", room, argv[2]);
    return 0;
}

/* Command table (name, "help" text, handler) registered with esp_console. */
void cli_register_sim_cmds(void)
{
    static const esp_console_cmd_t cmds[] = {
        { .command = "tap",  .help = "Simulate a card at a reader: tap <UID> in|out", .func = cmd_tap },
        { .command = "node", .help = "Simulate a room node link failure: node <1|2> offline|online", .func = cmd_node },
    };
    cli_register(cmds, COUNT_OF(cmds));
}
