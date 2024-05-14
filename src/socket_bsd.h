// socket_bsd.h
#ifndef SOCKET_BSD_H
#define SOCKET_BSD_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/event.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

// 设置 socket 为非阻塞模式
static inline void 
set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1) {
        //设置旗标 O_NONBLOCK
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
} 

// 添加 socket 到 kqueue
static inline int 
kqueue_add(int kq, int fd, void *ptr, int filter, int flags) {
    struct kevent ev;
    EV_SET(&ev, fd, filter, flags, 0, 0, ptr);
    return kevent(kq, &ev, 1, NULL, 0, NULL);
}

// 删除 socket 从 kqueue
static inline int 
kqueue_del(int kq, int fd, int filter) {
    struct kevent ev;
    EV_SET(&ev, fd, filter, EV_DELETE, 0, 0, NULL);
    return kevent(kq, &ev, 1, NULL, 0, NULL);
}

static inline int
kqueue_wait(int kq, struct kevent *events, int max_events, int timeout_ms) {
    struct timespec timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_nsec = (timeout_ms % 1000) * 1000000;
    int ev_num = kevent(kq, NULL, 0, events, max_events, &timeout);
    return ev_num; // 返回处理的事件数量
}

static inline void 
kqueue_enable_read(int kq, int fd, void *ud, bool enable) {
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_READ, enable ? EV_ENABLE : EV_DISABLE, 0, 0, ud);
    kevent(kq, &ev, 1, NULL, 0, NULL);
}

static inline void 
kqueue_enable_write(int kq, int fd, void *ud, bool enable) {
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_WRITE, enable ? EV_ENABLE : EV_DISABLE, 0, 0, ud);
    kevent(kq, &ev, 1, NULL, 0, NULL);
}

#endif // SOCKET_BSD_H