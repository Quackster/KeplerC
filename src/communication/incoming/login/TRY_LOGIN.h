#include "log.h"

#include "communication/messages/incoming_message.h"
#include "communication/messages/outgoing_message.h"

#include "game/player/player_refresh.h"
#include "database/queries/player_query.h"
#include "server/server_listener.h"
#include "dispatch.h"

/*
 * Arguments we will send the to the login thread
 */
typedef struct login_context_s {
    char username[255];
    char password[255];
    entity *player;
} login_context;

typedef struct login_result_s {
    entity *player;
    int player_id;
    entity_data *data;
} login_result;

void do_login_apply(void *args) {
    login_result *result = (login_result *) args;
    entity *player = result->player;

    if (!player->disconnected) {
        if (result->player_id == -1) {
            player_send_localised_error(player, "login incorrect");
        } else {
            player->details = result->data;
            result->data = NULL;

            player_manager_destroy_session_by_id(result->player_id);
            player_login(player);
        }
    }

    if (result->data != NULL) {
        player_data_cleanup(result->data);
    }

    player_release(player);
    free(result);
}

/*
 * This function does the off-server-thread login
 */
void do_login(void *args) {
    login_context *ctx = (login_context *)args;
    login_result *result = malloc(sizeof(login_result));

    if (result == NULL) {
        player_release(ctx->player);
        free(ctx);
        return;
    }

    result->player = ctx->player;
    result->player_id = player_query_login(ctx->username, ctx->password);
    result->data = NULL;

    if (result->player_id != -1) {
        result->data = player_query_data(result->player_id);
    }

    if (server_dispatch(&do_login_apply, result) != 0) {
        if (result->data != NULL) {
            player_data_cleanup(result->data);
        }

        player_release(result->player);
        free(result);
    }

    free(ctx);
}

/*
 * Off-server-thread login, as the password hashing function blocks the server thread for too long
 *
 * @param username Login username
 * @param password Login password
 */
void async_login(char *username, char *password, entity *player) {
    login_context *ctx = malloc(sizeof(login_context));

    if (ctx == NULL) {
        return;
    }

    strcpy(ctx->username, username);
    strcpy(ctx->password, password);
    ctx->player = player;

    player_retain(player);

    if (hh_dispatch(StorageDispatch, do_login, (void*) ctx) != 0) {
        player_release(player);
        free(ctx);
    }
}

void TRY_LOGIN(entity *player, incoming_message *message) {
    char *username = im_read_str(message);
    char *password = im_read_str(message);

    if (username == NULL || password == NULL) {
        goto cleanup;
    }

    async_login(username, password, player);

    cleanup:
        free(username);
        free(password);
}
