#include "list.h"

#include "rcon_handler.h"
#include "rcon_listener.h"
#include "server/server_listener.h"

#include "game/player/player_manager.h"
#include "game/player/player.h"
#include "game/player/player_refresh.h"

#include "shared.h"
#include "log.h"

typedef struct rcon_player_id_s {
    int player_id;
} rcon_player_id_t;

typedef struct rcon_alert_s {
    char *message;
} rcon_alert_t;

static void rcon_refresh_appearance_cb(void *data) {
    rcon_player_id_t *ctx = data;

    entity *p = player_manager_find_by_id(ctx->player_id);

    if (p != NULL) {
        player_refresh_appearance(p);
    }

    free(ctx);
}

static void rcon_hotel_alert_cb(void *data) {
    rcon_alert_t *ctx = data;

    for (size_t i = 0; i < list_size(global.player_manager.players); i++) {
        entity *player;
        list_get_at(global.player_manager.players, i, (void *) &player);

        if (player->disconnected || !player->logged_in) {
            continue;
        }

        player_send_alert(player, ctx->message);
    }

    free(ctx->message);
    free(ctx);
}

void rcon_handle_command(uv_stream_t *handle, char header, char *message) {
    if (header == '1') { // "GET_USERS"
        char users_online[10];

        player_manager_lock();
        sprintf(users_online, "%i", (int) list_size(global.player_manager.players));
        player_manager_unlock();

        rcon_send(handle, users_online);
    }

    if (header == '2') { // "REFRESH_APPEARANCE"
        int player_id = (int)strtol(message, NULL, 10);

        log_debug("RCON: refresh appearance for user id %u", player_id);

        rcon_player_id_t *ctx = malloc(sizeof(rcon_player_id_t));

        if (ctx != NULL) {
            ctx->player_id = player_id;

            if (server_dispatch(&rcon_refresh_appearance_cb, ctx) != 0) {
                free(ctx);
            }
        }
    }

    if (header == 'h') { // "HOTEL_ALERT"
        log_debug("RCON: Mass hotel alert message: %s", message);

        rcon_alert_t *ctx = malloc(sizeof(rcon_alert_t));

        if (ctx != NULL) {
            ctx->message = strdup(message);

            if (ctx->message == NULL || server_dispatch(&rcon_hotel_alert_cb, ctx) != 0) {
                free(ctx->message);
                free(ctx);
            }
        }
    }
}

void rcon_send(uv_stream_t *handle, char *data) {
    if (handle == NULL || uv_is_closing((uv_handle_t *) handle)) {
        return;
    }

    uv_write_t *req;

    if(!(req = malloc(sizeof(uv_write_t)))){
        return;
    }

    size_t message_length = strlen(data);

    uv_buf_t buffer = uv_buf_init(malloc(message_length), (unsigned int) message_length);
    memcpy(buffer.base, data, message_length);

    req->handle = handle;
    req->data = buffer.base;

    int response = uv_write(req, handle, &buffer, 1, &rcon_on_write);

    if (response != 0) {
        free(buffer.base);
        free(req);
    }
}
