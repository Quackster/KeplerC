#ifndef SERVER_LISTENER_H
#define SERVER_LISTENER_H

#include <stdbool.h>

#include "uv.h"

typedef struct server_settings_s {
    char ip[255];
    int port;
} server_settings;

typedef void (*server_dispatch_cb_t)(void *data);

int server_dispatch(server_dispatch_cb_t cb, void *data);
bool server_is_server_thread();
void server_shutdown(uv_thread_t *server_thread);
void server_on_new_connection(uv_stream_t *server, int status);
void server_on_read(uv_stream_t *handle, ssize_t nread, const uv_buf_t *buf);
void server_alloc_buffer(uv_handle_t* handle, size_t  size, uv_buf_t* buf);
void server_on_connection_close(uv_handle_t *handle);
void server_on_write(uv_write_t* req, int status);
void start_server(server_settings *settings, uv_thread_t *server_thread);

#endif
