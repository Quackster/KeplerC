#include "shared.h"

#include "list.h"
#include "player.h"

#include "database/queries/player_query.h"
#include "server/server_listener.h"
/**
 * Create a new list to store players
 */
void player_manager_init() {
    list_new(&global.player_manager.players);
    uv_mutex_init(&global.player_manager.mutex);
}

void player_manager_lock() {
    uv_mutex_lock(&global.player_manager.mutex);
}

void player_manager_unlock() {
    uv_mutex_unlock(&global.player_manager.mutex);
}

/**
 * Creates a new player when given a new network stream to add. Will return a
 * old player if the stream was already added.
 *
 * @param stream the dyad stream
 * @return the player
 */
entity *player_manager_add(void *stream, char *ip) {
    entity *p = player_create(stream, ip);
    player_manager_lock();
    list_add(global.player_manager.players, p);
    player_manager_unlock();
    return p;
}

/**
 * Removes a player by the given stream
 * @param stream the dyad stream
 */
void player_manager_remove(entity *p) {
    player_manager_lock();
    if (list_contains(global.player_manager.players, p)) {
        list_remove(global.player_manager.players, p, NULL);
    }
    player_manager_unlock();
}

/**
 * Find a player by user id
 *
 * @param player_id the player id
 * @return the player, if sound, otherwise returns NULL
 */
entity *player_manager_find_by_id(int player_id) {
    player_manager_lock();

    if (list_size(global.player_manager.players) > 0) {
        for (size_t i = 0; i < list_size(global.player_manager.players); i++) {
            entity *p;
            list_get_at(global.player_manager.players, i, (void *) &p);

            if (!p->logged_in) {
                continue;
            }

            if (p->details->id == player_id) {
                player_manager_unlock();
                return p;
            }
        }
    }

    player_manager_unlock();
    return NULL;
}

/**
 * Find a player by user id
 *
 * @param player_id the player id
 * @return the player, if sound, otherwise returns NULL
 */
entity *player_manager_find_by_name(char *name) {
    player_manager_lock();

    if (list_size(global.player_manager.players) > 0) {
        for (size_t i = 0; i < list_size(global.player_manager.players); i++) {
            entity *p;
            list_get_at(global.player_manager.players, i, (void *) &p);

            if (!p->logged_in) {
                continue;
            }

            if (strcmp(p->details->username, name) == 0) {
                player_manager_unlock();
                return p;
            }
        }
    }

    player_manager_unlock();
    return NULL;
}

/**
 * Find a player by user id
 *
 * @param player_id the player id
 * @return the player, if sound, otherwise returns NULL
 */
entity_data *player_manager_get_data_by_id(int player_id) {
    player_manager_lock();

    if (list_size(global.player_manager.players) > 0) {
        for (size_t i = 0; i < list_size(global.player_manager.players); i++) {
            entity *p;
            list_get_at(global.player_manager.players, i, (void *) &p);

            if (!p->logged_in) {
                continue;
            }

            if (p->details->id == player_id) {
                player_manager_unlock();
                return (entity_data *) p->details;
            }
        }
    }

    player_manager_unlock();
    return player_query_data(player_id);
}

/**
* Destroy session by player id
*
* @param player_id the player id
*/
void player_manager_destroy_session_by_id(int player_id) {
    player_manager_lock();

    for (size_t i = 0; i < list_size(global.player_manager.players); i++) {
        entity *p;
        list_get_at(global.player_manager.players, i, (void*)&p);

        if (!p->logged_in || p->details->id != player_id) {
            continue;
        }

        player_disconnect(p, true);
    }

    player_manager_unlock();
}

/**
 * Dispose model manager
 */
void player_manager_dispose() {
    player_manager_lock();

    for (size_t i = 0; i < list_size(global.player_manager.players); i++) {
        entity *player;
        list_get_at(global.player_manager.players, i, (void *) &player);
        player_disconnect(player, false);
    }

    list_destroy(global.player_manager.players);
    player_manager_unlock();
    uv_mutex_destroy(&global.player_manager.mutex);
}
