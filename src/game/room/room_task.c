#include "room_task.h"
#include "log.h"
#include "list.h"

#include "shared.h"
#include "game/room/room.h"
#include "game/room/tasks/roller_task.h"
#include "game/room/tasks/status_task.h"
#include "game/room/tasks/walk_task.h"

/**
 * Start all room tasks if they haven't started already.
 *
 * @param room the room for the task to run inside
 */
void room_start_tasks(room *room) {
    (void) room;
}

/**
 * Stop all room tasks if they haven't stopped already.
 *
 * @param room the room for the task to run inside
 */
void room_stop_tasks(room *room) {
    (void) room;
}
