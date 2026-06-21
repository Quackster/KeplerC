#include "dispatch.h"

#include <stdbool.h>
#include <stdlib.h>

int runner_count = 0;
hh_dispatch_loop_group_t game_dispatch;
hh_dispatch_loop_group_t room_dispatch;
hh_dispatch_loop_group_t storage_dispatch;

typedef struct dispatch_timer_start_s {
    hh_dispatch_timer_t *timer;
    int initial_delay;
    int delay;
    int result;
    uv_sem_t done;
} dispatch_timer_start_t;

typedef struct dispatch_timer_close_s {
    hh_dispatch_timer_t *timer;
    uv_sem_t done;
    bool wait;
} dispatch_timer_close_t;

static void dispatch_timer_start_cb(void *data);
static void dispatch_timer_stop_cb(void *data);
static int dispatch_enqueue_internal(hh_dispatch_loop_t *loop, hh_dispatch_cb_t cb, void *data, bool allow_closing);
static void dispatch_shutdown_group(hh_dispatch_loop_group_t *group);
static void dispatch_shutdown_loop_cb(void *data);
static void dispatch_close_walk_cb(uv_handle_t *handle, void *arg);
static void dispatch_close_handle_cb(uv_handle_t *handle);

static void dispatch_exec_scheduled_callback(uv_timer_t *handle) {
    if (handle->data != NULL) {
        hh_dispatch_timer_t *timer = (hh_dispatch_timer_t *) handle->data;
        timer->work->cb(timer->work->data);
    }
}

static void dispatch_close_timer_cb(uv_handle_t *handle) {
    dispatch_timer_close_t *close_ctx = (dispatch_timer_close_t *) handle->data;
    hh_dispatch_timer_t *timer = close_ctx->timer;

    free(timer->work);
    free(timer->handle);
    free(timer);

    if (close_ctx->wait) {
        uv_sem_post(&close_ctx->done);
    } else {
        free(close_ctx);
    }
}

static void dispatch_exec_callback(uv_async_t *handle) {
    hh_dispatch_loop_t *loop = (hh_dispatch_loop_t *) handle->data;

    while (1) {
        uv_mutex_lock(&loop->mutex);
        hh_dispatch_work_t *work = loop->work_head;

        if (work != NULL) {
            loop->work_head = work->next;

            if (loop->work_head == NULL) {
                loop->work_tail = NULL;
            }
        }

        uv_mutex_unlock(&loop->mutex);

        if (work == NULL) {
            break;
        }

        work->cb(work->data);
        free(work);
    }
}

static int dispatch_enqueue(hh_dispatch_loop_t *loop, hh_dispatch_cb_t cb, void *data) {
    return dispatch_enqueue_internal(loop, cb, data, false);
}

static int dispatch_enqueue_internal(hh_dispatch_loop_t *loop, hh_dispatch_cb_t cb, void *data, bool allow_closing) {
    if (loop == NULL || cb == NULL) {
        return 1;
    }

    hh_dispatch_work_t *work = malloc(sizeof(hh_dispatch_work_t));

    if (work == NULL) {
        return 1;
    }

    work->cb = cb;
    work->data = data;
    work->next = NULL;

    uv_mutex_lock(&loop->mutex);

    if (loop->closing && !allow_closing) {
        uv_mutex_unlock(&loop->mutex);
        free(work);
        return 1;
    }

    if (loop->work_tail == NULL) {
        loop->work_head = work;
        loop->work_tail = work;
    } else {
        loop->work_tail->next = work;
        loop->work_tail = work;
    }

    uv_mutex_unlock(&loop->mutex);

    uv_async_send(&loop->async);
    return 0;
}

static void dispatch_initialise_loop_thread(void *data) {
    hh_dispatch_loop_t *loop = (hh_dispatch_loop_t *) data;

    loop->thread_id = uv_thread_self();

    uv_loop_init(loop->loop);

    loop->async.data = loop;
    uv_async_init(loop->loop, &loop->async, &dispatch_exec_callback);

    uv_sem_post(&loop->ready);
    uv_run(loop->loop, UV_RUN_DEFAULT);
}

static void dispatch_initialise_loops(hh_dispatch_loop_group_t *group, int count) {
    group->total_loops = count;
    group->current_index = -1;
    group->mutex = malloc(sizeof(uv_mutex_t));

    uv_mutex_init(group->mutex);

    group->loops = calloc((size_t) count, sizeof(hh_dispatch_loop_t *));

    for (int i = 0; i < count; i++) {
        hh_dispatch_loop_t *loop = malloc(sizeof(hh_dispatch_loop_t));

        loop->id = ++runner_count;
        loop->loop = malloc(sizeof(uv_loop_t));
        loop->thread = malloc(sizeof(uv_thread_t));
        loop->work_head = NULL;
        loop->work_tail = NULL;
        loop->closing = false;

        uv_mutex_init(&loop->mutex);
        uv_sem_init(&loop->ready, 0);

        uv_thread_create(loop->thread, &dispatch_initialise_loop_thread, loop);
        uv_sem_wait(&loop->ready);

        group->loops[i] = loop;
    }
}

void hh_dispatch_initialise(int game_dispatch_count, int room_dispatch_count,
                            int storage_dispatch_count) {
    dispatch_initialise_loops(&game_dispatch, game_dispatch_count);
    dispatch_initialise_loops(&room_dispatch, room_dispatch_count);
    dispatch_initialise_loops(&storage_dispatch, storage_dispatch_count);
}

void hh_dispatch_shutdown() {
    dispatch_shutdown_group(&game_dispatch);
    dispatch_shutdown_group(&room_dispatch);
    dispatch_shutdown_group(&storage_dispatch);
}

static void dispatch_shutdown_group(hh_dispatch_loop_group_t *group) {
    for (int i = 0; i < group->total_loops; i++) {
        hh_dispatch_loop_t *loop = group->loops[i];

        uv_mutex_lock(&loop->mutex);
        loop->closing = true;
        uv_mutex_unlock(&loop->mutex);

        dispatch_enqueue_internal(loop, &dispatch_shutdown_loop_cb, loop, true);
    }

    for (int i = 0; i < group->total_loops; i++) {
        uv_thread_join(group->loops[i]->thread);
    }
}

static void dispatch_shutdown_loop_cb(void *data) {
    hh_dispatch_loop_t *loop = (hh_dispatch_loop_t *) data;
    uv_walk(loop->loop, &dispatch_close_walk_cb, NULL);
}

static void dispatch_close_walk_cb(uv_handle_t *handle, void *arg) {
    (void) arg;

    if (!uv_is_closing(handle)) {
        uv_close(handle, &dispatch_close_handle_cb);
    }
}

static void dispatch_close_handle_cb(uv_handle_t *handle) {
    (void) handle;
}

static hh_dispatch_loop_group_t *dispatch_get_group(char group) {
    switch (group) {
        default:
            return &game_dispatch;

        case GameDispatch:
            return &game_dispatch;
        case RoomDispatch:
            return &room_dispatch;
        case StorageDispatch:
            return &storage_dispatch;
    }
}

static hh_dispatch_loop_t *dispatch_group_next_loop(hh_dispatch_loop_group_t *group) {
    hh_dispatch_loop_t *loop;

    uv_mutex_lock(group->mutex);

    if ((group->current_index + 1) == group->total_loops) {
        group->current_index = 0;
    } else {
        group->current_index++;
    }

    loop = group->loops[group->current_index];
    uv_mutex_unlock(group->mutex);

    return loop;
}

int hh_dispatch(char group_id, hh_dispatch_cb_t cb, void *data) {
    hh_dispatch_loop_group_t *group = dispatch_get_group(group_id);
    hh_dispatch_loop_t *loop = dispatch_group_next_loop(group);

    return dispatch_enqueue(loop, cb, data);
}

static void dispatch_timer_init_cb(void *data) {
    hh_dispatch_timer_t *timer = (hh_dispatch_timer_t *) data;

    uv_timer_init(timer->loop->loop, timer->handle);
    timer->handle->data = timer;
}

hh_dispatch_timer_t *hh_dispatch_timer_create(char group_id, hh_dispatch_cb_t cb, void *data) {
    hh_dispatch_loop_group_t *group = dispatch_get_group(group_id);
    hh_dispatch_loop_t *loop = dispatch_group_next_loop(group);

    if (loop == NULL || cb == NULL) {
        return NULL;
    }

    hh_dispatch_timer_t *timer = malloc(sizeof(hh_dispatch_timer_t));
    hh_dispatch_work_t *work = malloc(sizeof(hh_dispatch_work_t));
    uv_timer_t *handle = malloc(sizeof(uv_timer_t));

    if (timer == NULL || work == NULL || handle == NULL) {
        free(timer);
        free(work);
        free(handle);
        return NULL;
    }

    work->cb = cb;
    work->data = data;
    work->next = NULL;

    timer->work = work;
    timer->handle = handle;
    timer->loop = loop;

    uv_thread_t self = uv_thread_self();

    if (uv_thread_equal(&self, &loop->thread_id)) {
        uv_timer_init(loop->loop, handle);
        handle->data = timer;
        return timer;
    }

    if (dispatch_enqueue(loop, &dispatch_timer_init_cb, timer) != 0) {
        free(work);
        free(handle);
        free(timer);
        return NULL;
    }

    return timer;
}

int hh_dispatch_timer_start(hh_dispatch_timer_t *handle, int initial_delay, int delay) {
    if (handle == NULL) {
        return -1;
    }

    uv_thread_t self = uv_thread_self();

    if (uv_thread_equal(&self, &handle->loop->thread_id)) {
        return uv_timer_start(handle->handle,
                              &dispatch_exec_scheduled_callback,
                              (uint64_t) initial_delay,
                              (uint64_t) delay);
    }

    dispatch_timer_start_t ctx = {
        .timer = handle,
        .initial_delay = initial_delay,
        .delay = delay,
        .result = -1
    };

    uv_sem_init(&ctx.done, 0);

    if (dispatch_enqueue(handle->loop, &dispatch_timer_start_cb, &ctx) != 0) {
        uv_sem_destroy(&ctx.done);
        return -1;
    }

    uv_sem_wait(&ctx.done);
    uv_sem_destroy(&ctx.done);

    return ctx.result;
}

static void dispatch_timer_start_cb(void *data) {
    dispatch_timer_start_t *ctx = (dispatch_timer_start_t *) data;

    ctx->result = uv_timer_start(ctx->timer->handle,
                                 &dispatch_exec_scheduled_callback,
                                 (uint64_t) ctx->initial_delay,
                                 (uint64_t) ctx->delay);
    uv_sem_post(&ctx->done);
}

int hh_dispatch_timer_dispose(hh_dispatch_timer_t *handle) {
    if (handle == NULL) {
        return -1;
    }

    uv_thread_t self = uv_thread_self();

    if (uv_thread_equal(&self, &handle->loop->thread_id)) {
        dispatch_timer_close_t *ctx = malloc(sizeof(dispatch_timer_close_t));

        if (ctx == NULL) {
            return -1;
        }

        ctx->timer = handle;
        ctx->wait = false;

        dispatch_timer_stop_cb(ctx);
        return 0;
    }

    dispatch_timer_close_t ctx = {
        .timer = handle,
        .wait = true
    };

    uv_sem_init(&ctx.done, 0);

    if (dispatch_enqueue(handle->loop, &dispatch_timer_stop_cb, &ctx) != 0) {
        uv_sem_destroy(&ctx.done);
        return -1;
    }

    uv_sem_wait(&ctx.done);
    uv_sem_destroy(&ctx.done);

    return 0;
}

static void dispatch_timer_stop_cb(void *data) {
    dispatch_timer_close_t *ctx = (dispatch_timer_close_t *) data;

    uv_timer_stop(ctx->timer->handle);
    ctx->timer->handle->data = ctx;
    uv_close((uv_handle_t *) ctx->timer->handle, &dispatch_close_timer_cb);
}
