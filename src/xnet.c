#include "xnet.h"
#include <assert.h>
#include <pthread.h>

#define CMD_TYPE_EXIT 0
#define CMD_TYPE_LISTEN 1
#define CMD_TYPE_CLOSE 2
#define CMD_TYPE_CONNECT 3
#define CMD_TYPE_SNED_TCP_PKG 4
#define CMD_TYPE_USER_COMMAND 128

typedef struct {
    char *filename;
    FILE *stdlog;
} log_context_t;

static xnet_context_t *g_log_ctx = NULL;
static log_context_t g_log_context;

static int
log_command_func(xnet_context_t *ctx, xnet_context_t *source, int command, void *data, int sz) {
    char time_str[128];
    timestring(ctx->nowtime/1000, time_str, sizeof(time_str));
    fprintf(g_log_context.stdlog, "[%d][%s.%03d]:", command, time_str, ctx->nowtime%1000);
    fwrite(data, sz, 1, g_log_context.stdlog);
    fprintf(g_log_context.stdlog, "\n");
    return 0;
}

static void *
thread_log(void *p) {
    xnet_context_t *ctx = p;
    xnet_register_command(ctx, log_command_func);
    xnet_dispatch_loop(ctx);
}

static void
log_init(char *log_filename) {
    pthread_t pid;

    if (log_filename) {
        g_log_context.stdlog = fopen(log_filename, "a");
        if (!g_log_context.stdlog) {
            perror("open log file error");
            g_log_context.stdlog = stdout;
        }
        g_log_context.filename = strdup(log_filename);
    } else {
        g_log_context.filename = NULL;
        g_log_context.stdlog = stdout;
    }

    g_log_ctx = xnet_create_context();
    if (!g_log_ctx) return;
    pthread_create(&pid, NULL, thread_log, g_log_ctx);
    pthread_detach(pid);
}

static void
log_deinit() {
    if (g_log_ctx) xnet_release_context(g_log_ctx);
    if (g_log_context.filename) {
        free(g_log_context.filename);
    }
    if (g_log_context.stdlog != stdout) {
        fclose(g_log_context.stdlog);
    }
}

static void
update_time_cache(xnet_context_t *ctx) {
    ctx->nowtime = get_time();
}

static void
deal_with_timeout_event(xnet_context_t *ctx) {
    xnet_timeinfo_t ti;
    uint64_t nowtime;
    if (!ctx->timeout_func) return;
    nowtime = ctx->nowtime;

    while (xnet_timeheap_top(&ctx->th, &ti)) {
        if (ti.expire > nowtime) break;
        xnet_timeheap_pop(&ctx->th, NULL);
        ctx->timeout_func(ctx, ti.id);
    }
}

static void
send_cmd(xnet_context_t *ctx, xnet_cmdreq_t *req, int type, int len) {
    SOCKET_TYPE fd = ctx->poll.send_fd;
    req->header[6] = (uint8_t)type;
    req->header[7] = (uint8_t)len;
    block_send(fd, &req->header[6], len+2);
}

static void
cmd_listen(xnet_context_t *ctx, xnet_cmdreq_listen_t *req) {
    xnet_socket_t *s = NULL;
    int ret;
    if (xnet_listen_tcp_socket(&ctx->poll, req->port, &s) != 0) {
        ret = -1;
    } else {
        ret = s->id;
    }

    if (req->source && req->back_command) {
        xnet_send_command(req->source, ctx, req->back_command, NULL, ret);
    }
}

static void
cmd_close(xnet_context_t *ctx, xnet_cmdreq_close_t *req) {
    xnet_socket_t *s = &ctx->poll.slots[req->id];
    if (s->type == SOCKET_TYPE_INVALID || s->closing)
        return;

    xnet_close_socket(ctx, s);
}

static void
cmd_connect(xnet_context_t *ctx, xnet_cmdreq_connect_t *req) {
    xnet_socket_t *s = NULL;
    int ret;
    if (xnet_connect_tcp_socket(&ctx->poll, req->host, req->port, &s) != 0) {
        ret = -1;
    } else {
        ret = s->id;
    }
    if (req->source && req->back_command) {
        //注意这里只是返回id，不一定就是已经连接成功
        xnet_send_command(req->source, ctx, req->back_command, NULL, ret);
    }
}

static void
cmd_send_tcp_pkg(xnet_context_t *ctx, xnet_cmdreq_sendtcp_t *req) {
    xnet_socket_t *s = &ctx->poll.slots[req->id];
    if (s->type == SOCKET_TYPE_INVALID || s->closing) {
        free(req->data);
        return;
    }

    append_send_buff(&ctx->poll, s, req->data, req->size);
}

static void
cmd_command(xnet_context_t *ctx, xnet_cmdreq_command_t *req) {
    assert(ctx->command_func);

    if (ctx->command_func(ctx, req->source, req->command, req->data, req->size) == 0) {
        free(req->data);
    }
}

static int
has_cmd(xnet_context_t *ctx) {
    struct timeval tv = {0,0};
    int retval;
    SOCKET_TYPE recv_fd = ctx->poll.recv_fd;

    FD_SET(recv_fd, &ctx->poll.rfds);
    retval = select(recv_fd, &ctx->poll.rfds, NULL, NULL, &tv);
    if (retval == 1) {
        return 1;
    }
    return 0;
}

static int
ctrl_cmd(xnet_context_t *ctx) {
    SOCKET_TYPE fd = ctx->poll.recv_fd;
    uint8_t *buffer[256];
    uint8_t header[2];
    int type, len;

    block_recv(fd, header, sizeof(header));
    type = header[0]; len = header[1];
printf("ctrl_cmd[%d, %d, %d]\n", type, len, sizeof(header));
    if (len > 0) block_recv(fd, buffer, len);

    switch (type) {
        case CMD_TYPE_EXIT:
            return 0;
        case CMD_TYPE_LISTEN:
            cmd_listen(ctx, (xnet_cmdreq_listen_t *)buffer);
        break;
        case CMD_TYPE_CLOSE:
            cmd_close(ctx, (xnet_cmdreq_close_t *)buffer);
        case CMD_TYPE_CONNECT:
            cmd_connect(ctx, (xnet_cmdreq_connect_t *)buffer);
        break;
        case CMD_TYPE_SNED_TCP_PKG:
            cmd_send_tcp_pkg(ctx, (xnet_cmdreq_sendtcp_t *)buffer);
        break;
        case CMD_TYPE_USER_COMMAND:
            cmd_command(ctx, (xnet_cmdreq_command_t *)buffer);
        break;
    }
    return -1;
}

int
xnet_init() {
    xnet_socket_init();
    log_init(NULL);
    return 0;
}


int
xnet_deinit() {
    xnet_socket_deinit();
    log_deinit();
    return 0;
}


xnet_context_t *
xnet_create_context() {
    static int alloc_id = 0;
    xnet_context_t *ctx;

    ctx = (xnet_context_t *)malloc(sizeof(xnet_context_t));
    if (!ctx) return NULL;

    //xnet_mq_init(&ctx->mq);
    if (xnet_poll_init(&ctx->poll) != 0) {
        goto FAILED;
    }
    xnet_timeheap_init(&ctx->th);
    update_time_cache(ctx);

    alloc_id += 1;
    ctx->id = alloc_id;
    ctx->listen_func = NULL;
    ctx->recv_func = NULL;
    ctx->error_func = NULL;
    ctx->connect_func = NULL;
    ctx->timeout_func = NULL;
    ctx->command_func = NULL;
    return ctx;
FAILED:
    free(ctx);
    return NULL;
}

void
xnet_release_context(xnet_context_t *ctx) {
    xnet_timeheap_release(&ctx->th);
    free(ctx);
}

#define LOG_MESSAGE_SIZE 256
void
xnet_error(xnet_context_t *ctx, char *msg, ...) {
    char tmp[LOG_MESSAGE_SIZE];
    va_list ap;
    int len, max_size;
    char *data;

    if (!g_log_ctx) return;

    va_start(ap, msg);
    len = vsnprintf(tmp, sizeof(tmp), msg, ap);
    va_end(ap);
    if (len < 0) return;
    if (len < LOG_MESSAGE_SIZE) {
        data = strdup(tmp); 
    } else {
        max_size = LOG_MESSAGE_SIZE;
        for (;;) {
            max_size *= 2;
            data = malloc(max_size);
            va_start(ap, msg);
            len = vsnprintf(data, max_size, msg, ap);
            va_end(ap);
            if (len < max_size) break;
            free(data);
        }
    }
    if (len < 0) {
        free(data);
        return;
    }
    xnet_send_command(g_log_ctx, ctx, ctx->id, data, len);
}

void
xnet_register_listener(xnet_context_t *ctx, xnet_listen_func_t listen_func, \
    xnet_error_func_t error_func, xnet_recv_func_t recv_func) {
    ctx->listen_func = listen_func;
    ctx->error_func = error_func;
    ctx->recv_func = recv_func;
}

void
xnet_register_connecter(xnet_context_t *ctx, xnet_connect_func_t connect_func, \
    xnet_error_func_t error_func, xnet_recv_func_t recv_func) {
    ctx->connect_func = connect_func;
    ctx->error_func = error_func;
    ctx->recv_func = recv_func;
}

void
xnet_register_timeout(xnet_context_t *ctx, xnet_timeout_func_t timeout_func) {
    ctx->timeout_func = timeout_func;
}

void
xnet_register_command(xnet_context_t *ctx, xnet_command_func_t command_func) {
    ctx->command_func = command_func;
}

int
xnet_add_timer(xnet_context_t *ctx, int id, int timeout) {
    if (timeout < 0) return -1;
    assert(ctx->timeout_func);
    uint64_t expire = ctx->nowtime + (uint64_t)timeout;
    xnet_timeheap_push(&ctx->th, &(xnet_timeinfo_t){id, expire});
    return 0;
}

int
xnet_listen(xnet_context_t *ctx, int port, xnet_socket_t **socket_out) {
    return xnet_listen_tcp_socket(&ctx->poll, port, socket_out);
}

int
xnet_connect(xnet_context_t *ctx, char *host, int port, xnet_socket_t **socket_out) {
    return xnet_connect_tcp_socket(&ctx->poll, host, port, socket_out);
}

void
xnet_close_socket(xnet_context_t *ctx, xnet_socket_t *s) {
    //主动关闭连接
    if (wb_list_empty(s)) {
        xnet_poll_closefd(&ctx->poll, s);
        return;
    }
    s->closing = true;
}

static void
deal_with_connected(xnet_context_t *ctx, xnet_socket_t *s) {
    int error;
    socklen_t len = sizeof(error);
    int code = get_sockopt(s->fd, SOL_SOCKET, SO_ERROR, &error, &len);  
    if (code < 0 || error) {
        error = code < 0 ? errno : error;
        ctx->connect_func(ctx, s, error);
        xnet_poll_closefd(&ctx->poll, s);
        return;
    }

    s->type = SOCKET_TYPE_CONNECTED;
    xnet_enable_write(&ctx->poll, s, false);
    ctx->connect_func(ctx, s, 0);
}

int
xnet_dispatch_loop(xnet_context_t *ctx) {
    int ret, i;
    xnet_poll_event_t *poll_event = &ctx->poll.poll_event;
    xnet_poll_t *poll = &ctx->poll;
    xnet_socket_t *s, *ns;
    char *buffer;
    xnet_timeinfo_t ti;
    int timeout = -1;

    while (1) {
        while (has_cmd(ctx)) {
            ret = ctrl_cmd(ctx);
            if (ret == 0) {
                printf("exit loop\n");
                break;
            }
        }

        //处理定时事件
        update_time_cache(ctx);
        deal_with_timeout_event(ctx);

        if (xnet_timeheap_top(&ctx->th, &ti)) {
            assert(ti.expire > ctx->nowtime);
            timeout = (int)(ti.expire - ctx->nowtime);
        } else {
            timeout = -1;
        }

        ret = xnet_poll_wait(poll, timeout);
        if (ret < 0) {
            //have error?
            printf("xnet_poll_wait error:%d\n", ret);
            continue;
        }

        //对触发的事件进行处理
        for (i=0; i<poll_event->n; i++) {
            s = poll_event->s[i];
            if (!s) continue;

            if (s->type == SOCKET_TYPE_LISTENING) {
                ns = NULL;
                if (xnet_accept_tcp_socket(poll, s, &ns) == 0) {
                    ctx->listen_func(ctx, s, ns);
                } else {
                    xnet_error(ctx, "accept tcp socket have error:%d", s->id);
                }
            } else if(s->type == SOCKET_TYPE_CONNECTING) {
                deal_with_connected(ctx, s);
            } else {
                if (poll_event->read[i]) {
                    //输入缓存区有可读数据；处于监听状态，有连接到达；出错
                    xnet_error(ctx, "read event[%d]", s->id);
                    ret = xnet_recv_data(poll, s, &buffer);
                    if (ret > 0) {
                        if (ctx->recv_func(ctx, s, buffer, ret) == 0)
                            free(buffer);
                        buffer = NULL;
                    } else if (ret < 0) {
                        ctx->error_func(ctx, s, 0);
                        xnet_poll_closefd(poll, s);
                    }
                }

                if (poll_event->write[i]) {
                    //输出缓冲区可写；连接成功
                    //发送缓冲列表内的数据
                    xnet_error(ctx, "write event[%d]", s->id);
                    xnet_send_data(&ctx->poll, s);
                }

                if (poll_event->error[i]) {
                    //异常；带外数据
                    xnet_error(ctx, "poll event error:%d", s->id);
                    ctx->error_func(ctx, s, 1);
                    xnet_poll_closefd(&ctx->poll, s);
                }
#ifndef _WIN32
                if (poll_event->eof[i]) {
                    //epoll特有的标记
                    xnet_error(ctx, "poll event eof:%d", s->id);
                    ctx->error_func(ctx, s, 2);
                    xnet_poll_closefd(&ctx->poll, s);
                }
#endif
            }
        }
    }

    return 0;
}

//只能在主线程使用
void
xnet_send_buffer(xnet_context_t *ctx, xnet_socket_t *s, char *buffer, int sz) {
    if (s->type == SOCKET_TYPE_INVALID || s->closing)
        return;
    char *new_buffer = (char*)malloc(sz);
    memcpy(new_buffer, buffer, sz);
    append_send_buff(&ctx->poll, s, new_buffer, sz);
}

int
xnet_send_command(xnet_context_t *ctx, xnet_context_t *source, int command, void *data, int sz) {
    xnet_cmdreq_t req;

    if (!ctx->command_func) {
        if (data) free(data);
        return -1;
    }

    req.pkg.command_req.source = source;
    req.pkg.command_req.command = command;
    req.pkg.command_req.data = data;
    req.pkg.command_req.size = sz;
    send_cmd(ctx, &req, CMD_TYPE_USER_COMMAND, sizeof(xnet_cmdreq_command_t));
    return 0;
}


int
xnet_asyn_listen(xnet_context_t *ctx, xnet_context_t *source, int port, int back_command) {
    xnet_cmdreq_t req;
    req.pkg.listen_req.source = source;
    req.pkg.listen_req.back_command = back_command;
    req.pkg.listen_req.port = port;
    send_cmd(ctx, &req, CMD_TYPE_LISTEN, sizeof(xnet_cmdreq_listen_t));
    return 0;
}

int
xnet_asyn_connect(xnet_context_t *ctx, xnet_context_t *source, char *host, int port, int back_command) {
    xnet_cmdreq_t req;
    size_t len = strlen(host);

    if (len + sizeof(xnet_cmdreq_connect_t) > 255) return -1;
    req.pkg.connect_req.source = source;
    req.pkg.connect_req.back_command = back_command;
    req.pkg.connect_req.port = port;
    strcpy(req.pkg.connect_req.host, host);
    send_cmd(ctx, &req, CMD_TYPE_CONNECT, sizeof(xnet_cmdreq_connect_t));
    return 0;
}

int
xnet_asyn_send_tcp_buffer(xnet_context_t *ctx, int id, char *buffer, int sz) {
    xnet_cmdreq_t req;
    req.pkg.send_tcp_req.id = id;
    req.pkg.send_tcp_req.data = buffer;//it will free by receiver
    req.pkg.send_tcp_req.size = sz;
    send_cmd(ctx, &req, CMD_TYPE_SNED_TCP_PKG, sizeof(xnet_cmdreq_sendtcp_t));
}

int
xnet_asyn_close_socket(xnet_context_t *ctx, int id) {
    xnet_cmdreq_t req;
    req.pkg.close_req.id = id;
    send_cmd(ctx, &req, CMD_TYPE_CLOSE, sizeof(xnet_cmdreq_close_t));
}

int
xnet_asyn_exit(xnet_context_t *ctx, xnet_context_t *source) {
    xnet_cmdreq_t req;
    req.pkg.exit_req.source = source;
    send_cmd(ctx, &req, CMD_TYPE_EXIT, sizeof(xnet_cmdreq_exit_t));
}