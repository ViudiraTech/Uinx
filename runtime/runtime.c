// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "runtime.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <pthread.h>
    #include <sched.h>
    #include <sys/socket.h>
    #include <time.h>
    #include <unistd.h>
#endif

static void* default_alloc(size_t size, size_t align, void* context) {
    (void)context;
    if (size == 0)
        size = 1;
#if defined(_WIN32)
    return _aligned_malloc(size, align < sizeof(void*) ? sizeof(void*) : align);
#else
    if (align <= _Alignof(max_align_t))
        return malloc(size);
    void* p = NULL;
    if (posix_memalign(&p, align, size) != 0)
        return NULL;
    return p;
#endif
}
static void default_free(void* ptr, size_t size, size_t align, void* context) {
    (void)size;
    (void)align;
    (void)context;
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}
static void*
default_realloc(void* ptr, size_t old_size, size_t new_size, size_t align, void* context) {
    (void)context;
    if (align <= _Alignof(max_align_t))
        return realloc(ptr, new_size);
    void* p = default_alloc(new_size, align, NULL);
    if (p && ptr)
        memcpy(p, ptr, old_size < new_size ? old_size : new_size);
    default_free(ptr, old_size, align, NULL);
    return p;
}
static uinx_allocator g_allocator = {default_alloc, default_realloc, default_free, NULL};
void uinx_set_allocator(uinx_allocator a) {
    if (a.alloc && a.realloc && a.free)
        g_allocator = a;
}
void* uinx_alloc(size_t size, size_t align) {
    return g_allocator.alloc(size, align, g_allocator.context);
}
void* uinx_realloc(void* p, size_t old_size, size_t new_size, size_t align) {
    return g_allocator.realloc(p, old_size, new_size, align, g_allocator.context);
}
void uinx_dealloc(void* p, size_t size, size_t align) {
    if (p)
        g_allocator.free(p, size, align, g_allocator.context);
}
void uinx_panic(const char* msg, const char* file, uint32_t line) {
    fprintf(
        stderr, "Uinx panic: %s (%s:%u)\n", msg ? msg : "panic", file ? file : "<unknown>", line);
    abort();
}
uint64_t uinx_time_monotonic_ns(void) {
#if defined(_WIN32)
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (uint64_t)((c.QuadPart * 1000000000ull) / (uint64_t)f.QuadPart);
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}
int64_t uinx_file_open(const char* path, int32_t flags, int32_t mode) {
#if defined(_WIN32)
    (void)path;
    (void)flags;
    (void)mode;
    return -ENOSYS;
#else
    int fd = open(path, flags, (mode_t)mode);
    return fd < 0 ? -(int64_t)errno : (int64_t)fd;
#endif
}
int64_t uinx_file_read(int64_t fd, void* buf, uint64_t len) {
#if defined(_WIN32)
    (void)fd;
    (void)buf;
    (void)len;
    return -ENOSYS;
#else
    ssize_t n;
    do {
        n = read((int)fd, buf, (size_t)len);
    } while (n < 0 && errno == EINTR);
    return n < 0 ? -(int64_t)errno : (int64_t)n;
#endif
}
int64_t uinx_file_write(int64_t fd, const void* buf, uint64_t len) {
#if defined(_WIN32)
    (void)fd;
    (void)buf;
    (void)len;
    return -ENOSYS;
#else
    ssize_t n;
    do {
        n = write((int)fd, buf, (size_t)len);
    } while (n < 0 && errno == EINTR);
    return n < 0 ? -(int64_t)errno : (int64_t)n;
#endif
}
int32_t uinx_file_close(int64_t fd) {
#if defined(_WIN32)
    (void)fd;
    return -ENOSYS;
#else
    return close((int)fd) == 0 ? 0 : -errno;
#endif
}
static void write_all(int32_t fd, const char* data, size_t len) {
    while (len != 0) {
        int64_t written = uinx_file_write(fd, data, len);
        if (written <= 0)
            return;
        data += (size_t)written;
        len -= (size_t)written;
    }
}
void uinx_print_cstr(const char* value, int32_t fd) {
    static const char empty[] = "(null)";
    if (!value)
        value = empty;
    write_all(fd, value, strlen(value));
}
void uinx_print_i64(int64_t value, int32_t fd) {
    char buffer[32];
    int len = snprintf(buffer, sizeof(buffer), "%" PRId64, value);
    if (len > 0)
        write_all(fd, buffer, (size_t)len);
}
void uinx_print_u64(uint64_t value, int32_t fd) {
    char buffer[32];
    int len = snprintf(buffer, sizeof(buffer), "%" PRIu64, value);
    if (len > 0)
        write_all(fd, buffer, (size_t)len);
}
void uinx_print_f64(double value, int32_t fd) {
    char buffer[64];
    int len = snprintf(buffer, sizeof(buffer), "%.15g", value);
    if (len > 0)
        write_all(fd, buffer, (size_t)len);
}
void uinx_print_bool(int64_t value, int32_t fd) {
    static const char true_value[] = "True";
    static const char false_value[] = "False";
    if (value)
        write_all(fd, true_value, sizeof(true_value) - 1);
    else
        write_all(fd, false_value, sizeof(false_value) - 1);
}
void uinx_print_char(uint32_t value, int32_t fd) {
    char bytes[4];
    size_t len = 0;
    if (!uinx_utf8_encode(value, (uint8_t*)bytes, &len))
        write_all(fd, bytes, len);
}
void uinx_print_space(int32_t fd) {
    write_all(fd, " ", 1);
}
void uinx_print_newline(int32_t fd) {
    write_all(fd, "\n", 1);
}
static _Thread_local char input_buffer[4096];
const char* uinx_input_cstr(const char* prompt, int32_t fd) {
    if (prompt)
        uinx_print_cstr(prompt, fd);
    size_t len = 0;
    int ch;
    while ((ch = fgetc(stdin)) != EOF && ch != '\n') {
        if (len + 1 < sizeof(input_buffer))
            input_buffer[len++] = (char)ch;
    }
    input_buffer[len] = '\0';
    return input_buffer;
}
size_t uinx_cstr_len(const char* value) {
    return value ? strlen(value) : 0;
}
int64_t uinx_parse_i64_cstr(const char* value) {
    if (!value)
        return 0;
    char* end = NULL;
    errno = 0;
    long long parsed = strtoll(value, &end, 10);
    if (errno != 0 || end == value)
        return 0;
    return (int64_t)parsed;
}
double uinx_parse_f64_cstr(const char* value) {
    if (!value)
        return 0.0;
    char* end = NULL;
    errno = 0;
    double parsed = strtod(value, &end);
    if (errno != 0 || end == value)
        return 0.0;
    return parsed;
}
static _Thread_local char string_ring[4][64];
static _Thread_local unsigned string_ring_index;
static char* next_string_slot(void) {
    char* slot = string_ring[string_ring_index++ % 4];
    slot[0] = '\0';
    return slot;
}
const char* uinx_string_from_i64(int64_t value) {
    char* buffer = next_string_slot();
    snprintf(buffer, 64, "%" PRId64, value);
    return buffer;
}
const char* uinx_string_from_u64(uint64_t value) {
    char* buffer = next_string_slot();
    snprintf(buffer, 64, "%" PRIu64, value);
    return buffer;
}
const char* uinx_string_from_f64(double value) {
    char* buffer = next_string_slot();
    snprintf(buffer, 64, "%.15g", value);
    return buffer;
}
const char* uinx_string_from_bool(int64_t value) {
    return value ? "True" : "False";
}
const char* uinx_string_from_char(uint32_t value) {
    char* buffer = next_string_slot();
    size_t len = 0;
    if (!uinx_utf8_encode(value, (uint8_t*)buffer, &len))
        buffer[0] = '\0';
    buffer[len] = '\0';
    return buffer;
}
const char* uinx_string_from_cstr(const char* value) {
    return value ? value : "";
}
int64_t uinx_socket(int32_t domain, int32_t type, int32_t protocol) {
#if defined(_WIN32)
    (void)domain;
    (void)type;
    (void)protocol;
    return -ENOSYS;
#else
    int fd = socket(domain, type, protocol);
    return fd < 0 ? -(int64_t)errno : fd;
#endif
}
int32_t uinx_socket_connect(int64_t fd, const void* addr, uint32_t len) {
#if defined(_WIN32)
    (void)fd;
    (void)addr;
    (void)len;
    return -ENOSYS;
#else
    return connect((int)fd, (const struct sockaddr*)addr, (socklen_t)len) == 0 ? 0 : -errno;
#endif
}
int64_t uinx_socket_send(int64_t fd, const void* buf, uint64_t len, int32_t flags) {
#if defined(_WIN32)
    (void)fd;
    (void)buf;
    (void)len;
    (void)flags;
    return -ENOSYS;
#else
    ssize_t n = send((int)fd, buf, (size_t)len, flags);
    return n < 0 ? -(int64_t)errno : n;
#endif
}
int64_t uinx_socket_recv(int64_t fd, void* buf, uint64_t len, int32_t flags) {
#if defined(_WIN32)
    (void)fd;
    (void)buf;
    (void)len;
    (void)flags;
    return -ENOSYS;
#else
    ssize_t n = recv((int)fd, buf, (size_t)len, flags);
    return n < 0 ? -(int64_t)errno : n;
#endif
}
#if !defined(_WIN32)
typedef struct {
    uinx_thread_fn fn;
    void* arg;
} thread_start;
static void* thread_entry(void* p) {
    thread_start* s = (thread_start*)p;
    uinx_thread_fn fn = s->fn;
    void* arg = s->arg;
    free(s);
    return (void*)(intptr_t)fn(arg);
}
#endif
int32_t uinx_thread_spawn(uinx_thread_fn fn, void* arg, uinx_thread_handle* out) {
    if (!fn || !out)
        return -EINVAL;
#if defined(_WIN32)
    (void)arg;
    return -ENOSYS;
#else
    pthread_t t;
    thread_start* s = (thread_start*)malloc(sizeof(*s));
    if (!s)
        return -ENOMEM;
    s->fn = fn;
    s->arg = arg;
    int rc = pthread_create(&t, NULL, thread_entry, s);
    if (rc) {
        free(s);
        return -rc;
    }
    memcpy(out, &t, sizeof(t) <= sizeof(*out) ? sizeof(t) : sizeof(*out));
    return 0;
#endif
}
int32_t uinx_thread_join(uinx_thread_handle h, intptr_t* result) {
#if defined(_WIN32)
    (void)h;
    (void)result;
    return -ENOSYS;
#else
    pthread_t t;
    memset(&t, 0, sizeof(t));
    memcpy(&t, &h, sizeof(t) <= sizeof(h) ? sizeof(t) : sizeof(h));
    void* r = NULL;
    int rc = pthread_join(t, &r);
    if (rc)
        return -rc;
    if (result)
        *result = (intptr_t)r;
    return 0;
#endif
}
void uinx_yield(void) {
#if defined(_WIN32)
    SwitchToThread();
#else
    sched_yield();
#endif
}
int32_t uinx_block_on(uinx_poll_fn poll, void* state, void* output) {
    if (!poll)
        return -EINVAL;
    for (;;) {
        int32_t r = poll(state, output);
        if (r != 0)
            return r > 0 ? 0 : r;
        uinx_yield();
    }
}

typedef struct {
    int32_t value;
    int32_t polled;
} uinx_yield_once_state;
static int32_t uinx_yield_once_poll(void* opaque, void* output) {
    uinx_yield_once_state* s = (uinx_yield_once_state*)opaque;
    if (!s || !output)
        return -EINVAL;
    if (!s->polled) {
        s->polled = 1;
        return 0;
    }
    *(int32_t*)output = s->value;
    return 1;
}
static void uinx_yield_once_drop(void* opaque) {
    if (opaque)
        uinx_dealloc(opaque, sizeof(uinx_yield_once_state), _Alignof(uinx_yield_once_state));
}
static const uinx_future_vtable uinx_yield_once_vtable = {uinx_yield_once_poll,
                                                          uinx_yield_once_drop};
uinx_future_raw uinx_yield_once_i32(int32_t value) {
    uinx_future_raw f = {0};
    uinx_yield_once_state* s =
        (uinx_yield_once_state*)uinx_alloc(sizeof(*s), _Alignof(uinx_yield_once_state));
    if (!s)
        return f;
    s->value = value;
    s->polled = 0;
    f.state = s;
    f.vtable = &uinx_yield_once_vtable;
    return f;
}
