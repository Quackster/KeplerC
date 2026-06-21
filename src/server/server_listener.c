#include <stdio.h>

#include "hashtable.h"
#include "shared.h"
#include "log.h"

#include "game/player/player.h"
#include "game/player/player_manager.h"

#include "communication/message_handler.h"

#include "communication/messages/incoming_message.h"
#include "communication/messages/outgoing_message.h"

#include "util/encoding/base64encoding.h"
#include "server/server_listener.h"

typedef struct server_dispatch_work_s {
    server_dispatch_cb_t cb;
    void *data;
    struct server_dispatch_work_s *next;
} server_dispatch_work_t;

static uv_async_t server_async;
static uv_mutex_t server_dispatch_mutex;
static uv_sem_t server_dispatch_ready;
static uv_thread_t server_thread_id;
static bool server_dispatch_closing = false;
static server_dispatch_work_t *server_work_head = NULL;
static server_dispatch_work_t *server_work_tail = NULL;

static void server_close_handle_cb(uv_handle_t *handle);
static void server_close_walk_cb(uv_handle_t *handle, void *arg);

static void server_dispatch_async_cb(uv_async_t *handle) {
    (void) handle;

    while (1) {
        uv_mutex_lock(&server_dispatch_mutex);
        server_dispatch_work_t *work = server_work_head;

        if (work != NULL) {
            server_work_head = work->next;

            if (server_work_head == NULL) {
                server_work_tail = NULL;
            }
        }

        uv_mutex_unlock(&server_dispatch_mutex);

        if (work == NULL) {
            break;
        }

        work->cb(work->data);
        free(work);
    }

    uv_mutex_lock(&server_dispatch_mutex);
    bool closing = server_dispatch_closing;
    uv_mutex_unlock(&server_dispatch_mutex);

    if (closing && global.server_loop != NULL) {
        uv_walk(global.server_loop, &server_close_walk_cb, NULL);
    }
}

int server_dispatch(server_dispatch_cb_t cb, void *data) {
    if (cb == NULL || global.server_loop == NULL) {
        return 1;
    }

    server_dispatch_work_t *work = malloc(sizeof(server_dispatch_work_t));

    if (work == NULL) {
        return 1;
    }

    work->cb = cb;
    work->data = data;
    work->next = NULL;

    uv_mutex_lock(&server_dispatch_mutex);

    if (server_dispatch_closing) {
        uv_mutex_unlock(&server_dispatch_mutex);
        free(work);
        return 1;
    }

    if (server_work_tail == NULL) {
        server_work_head = work;
        server_work_tail = work;
    } else {
        server_work_tail->next = work;
        server_work_tail = work;
    }

    uv_mutex_unlock(&server_dispatch_mutex);

    uv_async_send(&server_async);
    return 0;
}

bool server_is_server_thread() {
    uv_thread_t self = uv_thread_self();
    return global.server_loop != NULL && uv_thread_equal(&self, &server_thread_id);
}

static void server_close_handle_cb(uv_handle_t *handle) {
    if (handle->data != NULL) {
        server_on_connection_close(handle);
    }
}

static void server_close_walk_cb(uv_handle_t *handle, void *arg) {
    (void) arg;

    if (!uv_is_closing(handle)) {
        uv_close(handle, &server_close_handle_cb);
    }
}

void server_shutdown(uv_thread_t *server_thread) {
    if (global.server_loop == NULL) {
        return;
    }

    uv_mutex_lock(&server_dispatch_mutex);
    server_dispatch_closing = true;
    uv_mutex_unlock(&server_dispatch_mutex);

    uv_async_send(&server_async);
    uv_thread_join(server_thread);
}


/**
 * Allocate buffer for reading data.
 *
 * @param handle the socket that the data is going to
 * @param size the size of the data
 * @param buf the buffer containing the data
 */
void server_alloc_buffer(uv_handle_t* handle, size_t size, uv_buf_t* buf) {
    buf->base = malloc(size);
    buf->len = size;
}

/**
 * Handle connection close.
 *
 * @param handle the session that closed
 */
void server_on_connection_close(uv_handle_t *handle) {
    entity *player = handle->data;
    player->disconnected = true;

    log_info("Client [%s] has disconnected", player->ip_address);
    player_cleanup(player);
}

/**
 * Cleanup buffer after writing data.
 *
 * @param req the write request buffer
 * @param status the status of the write
 */
void server_on_write(uv_write_t* req, int status) {
    free(req->data);
    free(req);
}

/**
 * Read incoming data from socket.
 *
 * @param handle the socket to read from
 * @param nread the amount of bytes read
 * @param buf the buffer containing the data
 */
void server_on_read(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf) {
    if (nread == UV_EOF) {
        uv_close((uv_handle_t*) handle, server_on_connection_close);
        return;
    }

    if (nread == 0) {
        uv_close((uv_handle_t*) handle, server_on_connection_close);
        return;
    }

    if (nread > 0) {
        entity *player = handle->data;

        if (buf->base == NULL) {
            player->disconnected = true;
            return;
        }

        int amount_read = 0;

        while (amount_read < nread) {
            char recv_length[] = {
                    buf->base[amount_read++],
                    buf->base[amount_read++],
                    buf->base[amount_read++],
                    '\0'
            };

            int message_length = base64_decode(recv_length) + 1;


            if (message_length < 0 || message_length > 5120) {
                continue;
            }

            char *message = malloc(message_length * sizeof(char));

            for (int i = 0; i < message_length - 1; i++) {
                message[i] = buf->base[amount_read++];
            }

            message[message_length - 1] = '\0';

            if (player != NULL) {
                incoming_message *im = im_create(message);
                message_handler_invoke(im, player);
                im_cleanup(im);
            }

            free(message);
        }
    } else {
        uv_close((uv_handle_t *) handle, server_on_connection_close);
    }

    free(buf->base);
}

/**
 * Handle new connection handler.
 *
 * @param server the server to read the client
 * @param status the status of the client
 */
void server_on_new_connection(uv_stream_t *server, int status) {
    if (status == -1) {
        return;
    }

    uv_tcp_t *client = malloc(sizeof(uv_tcp_t));
    uv_tcp_init(server->loop, client);

    struct sockaddr_in client_addr;
    int client_addr_length;

    uv_stream_t *handle = (uv_stream_t*)client;
    uv_tcp_getpeername((const uv_tcp_t*) handle, (struct sockaddr*)&client_addr, &client_addr_length);

    char ip[256];
    uv_inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));

    entity *p = player_manager_add(handle, ip);
    client->data = p;

    log_info("Client [%s] has connected", p->ip_address);
    int result = uv_accept(server, handle);

    if(result == 0) {
        outgoing_message *msg = om_create(0); // "@@"
        player_send(p, msg);
        om_cleanup(msg);

        uv_read_start(handle, server_alloc_buffer, server_on_read);
    } else {
        uv_close((uv_handle_t *) handle, server_on_connection_close);
    }
}

/**
 * Thread callback to start server on a different loop.
 *
 * @param arguments the server settings argument
 * @param arguments the server settings argument
 */
void listen_server(void *arguments)  {
    server_settings *args = (server_settings *)arguments;
    server_thread_id = uv_thread_self();
    global.server_loop = uv_loop_new();
    uv_loop_t *loop = global.server_loop;

    uv_tcp_t server;
    struct sockaddr_in bind_addr;

    uv_async_init(loop, &server_async, &server_dispatch_async_cb);
    uv_sem_post(&server_dispatch_ready);

    uv_tcp_init(loop, &server);
    uv_ip4_addr(args->ip, args->port, &bind_addr);
    uv_tcp_bind(&server, (const struct sockaddr*) &bind_addr, 0);
    uv_listen((uv_stream_t *) &server, 128, server_on_new_connection);

    uv_run(loop, UV_RUN_DEFAULT);
    uv_loop_close(loop);
}

/**
 * Create thread for server listener.
 *
 * @param settings the server settings
 * @param server_thread the thread to initialise
 */
void start_server(server_settings *settings, uv_thread_t *server_thread) {
    log_info("Starting server on port %i...", settings->port);

    uv_mutex_init(&server_dispatch_mutex);
    uv_sem_init(&server_dispatch_ready, 0);
    server_dispatch_closing = false;

    if (uv_thread_create(server_thread, &listen_server, (void*) settings) != 0) {

        log_fatal("Uh-oh! Unable to spawn server thread");
    } else {
        uv_sem_wait(&server_dispatch_ready);
        log_info("Server successfully started!", settings->port);
    }
}
