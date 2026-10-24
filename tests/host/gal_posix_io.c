/*
 * gal_posix_io — ver gal_posix_io.h.
 */
#define _DEFAULT_SOURCE
#include "gal_posix_io.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

uint32_t pio_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void pio_init(pio_t *p)
{
    memset(p, 0, sizeof(*p));
    p->fd = -1;
}

static int io_connect(void *ctx, const char *host, uint16_t port, bool tls, uint32_t to_ms)
{
    pio_t *p = ctx;
    if (p->fd >= 0) {
        close(p->fd);
        p->fd = -1;
    }
    if (tls) {
        return GAL_IO_E_TLS; /* los servidores de prueba van por HTTP en 127.0.0.1 */
    }
    char ps[8];
    snprintf(ps, sizeof(ps), "%u", port);
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM}, *res = NULL;
    if (getaddrinfo(host, ps, &hints, &res) != 0 || !res) {
        return GAL_IO_E_DNS;
    }
    const int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        return GAL_IO_E_CONNECT;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    int r = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (r < 0 && errno == EINPROGRESS) {
        struct pollfd pf = {.fd = fd, .events = POLLOUT};
        r = poll(&pf, 1, (int)to_ms) == 1 ? 0 : -1;
        int err = 0;
        socklen_t el = sizeof(err);
        if (r == 0 && (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) != 0 || err != 0)) {
            r = -1;
        }
    }
    if (r < 0) {
        close(fd);
        return GAL_IO_E_CONNECT;
    }
    p->fd = fd;
    p->rd = p->wr = 0;
    p->connects++;
    return GAL_IO_OK;
}

static void cut(pio_t *p)
{
    if (p->fd >= 0) {
        shutdown(p->fd, SHUT_RDWR);
        close(p->fd);
        p->fd = -1;
    }
    p->cuts++;
}

static int io_write(void *ctx, const uint8_t *b, size_t n, uint32_t to_ms)
{
    pio_t *p = ctx;
    if (p->fd < 0) {
        return -1;
    }
    if (p->cut_write_after > 0 && p->wr + (long)n > p->cut_write_after) {
        p->cut_write_after = 0;
        cut(p);
        return -1;
    }
    struct pollfd pf = {.fd = p->fd, .events = POLLOUT};
    if (poll(&pf, 1, (int)to_ms) != 1) {
        return 0;
    }
    const ssize_t r = send(p->fd, b, n, MSG_NOSIGNAL);
    if (r < 0) {
        return errno == EAGAIN ? 0 : -1;
    }
    p->wr += r;
    p->total_wr += (uint64_t)r;
    return (int)r;
}

static int io_read(void *ctx, uint8_t *b, size_t n, uint32_t to_ms)
{
    pio_t *p = ctx;
    if (p->fd < 0) {
        return -1;
    }
    if (p->cut_after > 0 && p->rd >= p->cut_after) {
        p->cut_after = 0; /* una sola vez */
        cut(p);
        return -1;
    }
    if (p->cut_after > 0 && p->rd + (long)n > p->cut_after) {
        n = (size_t)(p->cut_after - p->rd);
    }
    struct pollfd pf = {.fd = p->fd, .events = POLLIN};
    const int pr = poll(&pf, 1, (int)to_ms);
    if (pr == 0) {
        return 0;
    }
    const ssize_t r = recv(p->fd, b, n, 0);
    if (r == 0) {
        return -1; /* el otro lado cerró */
    }
    if (r < 0) {
        return errno == EAGAIN ? 0 : -1;
    }
    p->rd += r;
    p->total_rd += (uint64_t)r;
    return (int)r;
}

static void io_close(void *ctx)
{
    pio_t *p = ctx;
    if (p->fd >= 0) {
        close(p->fd);
        p->fd = -1;
    }
}

static uint32_t io_now(void *ctx)
{
    (void)ctx;
    return pio_now_ms();
}

static bool io_cancelled(void *ctx)
{
    pio_t *p = ctx;
    return p->cancelled && p->cancelled(p->cancel_ud);
}

gal_io_t pio_io(pio_t *p)
{
    return (gal_io_t){.ctx = p, .connect = io_connect, .write = io_write, .read = io_read, .close = io_close,
                      .now = io_now, .cancelled = io_cancelled};
}
