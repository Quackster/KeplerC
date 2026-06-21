#include <unistd.h>
#include <time.h>
#include <stdlib.h>

#include "list.h"
#include "hashtable.h"
#include "log.h"

#include "game/player/player.h"
#include "game/room/room_user.h"
#include "game/room/room.h"
#include "game/room/tasks/roller_task.h"
#include "game/room/tasks/status_task.h"
#include "game/room/tasks/walk_task.h"
#include "game/room/manager/room_entity_manager.h"

#include "server/server_listener.h"

#include "game_thread.h"
#include "shared.h"

void game_thread_init(uv_thread_t *thread) {
    if (uv_thread_create(thread, game_thread_loop, NULL) != 0) {
        log_fatal("Uh-oh! Unable to spawn game thread");
    } else {
        log_info("Game thread successfully started!");
    }
}

void game_thread_loop(void *arguments) {
    unsigned long tick = 0;

    while (!global.is_shutdown) {
        tick++;
        unsigned long *tick_data = malloc(sizeof(unsigned long));

        if (tick_data != NULL) {
            *tick_data = tick;

            if (server_dispatch(&game_thread_task_dispatch, tick_data) != 0) {
                free(tick_data);
            }
        }

        usleep(500000);
    }

    printf("Game thread closed..\n");
}

void game_thread_task_dispatch(void *data) {
    if (data == NULL) {
        return;
    }

    unsigned long ticks = *((unsigned long *) data);
    free(data);

    game_thread_task(ticks);
}

void game_thread_task(unsigned long ticks) {
    for (size_t i = 0; i < list_size(global.player_manager.players); i++) {
        entity *player;
        list_get_at(global.player_manager.players, i, (void *) &player);

        if (player->disconnected) {
            continue;
        }

        // Check ping timeout
        if (ticks % 120 == 0) {
            if (player->ping_safe) {
                player->ping_safe = false;

                outgoing_message *om = om_create(50); // "@r"
                player_send(player, om);
                om_cleanup(om);
            } else {
                if (player->logged_in) {
                    log_info("Player %s timed out", player->details->username);
                } else {
                    log_info("Connection %s timed out", player->ip_address);
                }

                //uv_close((uv_handle_t*) player->stream, server_on_connection_close);
                //player_cleanup(player);
                player_disconnect(player, true);
                continue;
            }
        }

        if (player->logged_in) {
            if (player->room_user->room != NULL && time(NULL) > player->room_user->room_idle_timer) {
                room_leave(player->room_user->room, player, true); // Kick and send to hotel view
            }
        }
    }

    if (hashtable_size(global.room_manager.rooms) > 0) {
        HashTableIter iter;
        hashtable_iter_init(&iter, global.room_manager.rooms);

        TableEntry *entry;
        while (hashtable_iter_next(&iter, &entry) != CC_ITER_END) {
            room *room = entry->value;

            if (room == NULL || room->room_map == NULL || list_size(room->users) == 0) {
                continue;
            }

            walk_task(room);

            if (ticks % 2 == 0) {
                status_task(room);
            }

            if (ticks % 6 == 0) {
                roller_task(room);
            }
        }
    }
}
