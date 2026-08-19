// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#if !defined(_WIN32)
    #include <sys/wait.h>
    #include <unistd.h>
#endif

static intptr_t thread_add(void* opaque) {
    uint64_t* value = (uint64_t*)opaque;
    *value += 41;
    return 7;
}

int main(void) {
    uinx_vec_raw vec;
    if (uinx_vec_init(&vec, sizeof(uint32_t), _Alignof(uint32_t)))
        return 1;
    for (uint32_t i = 0; i < 1000; ++i)
        if (uinx_vec_push(&vec, &i))
            return 2;
    if (vec.len != 1000)
        return 3;
    uint32_t popped = 0;
    if (uinx_vec_pop(&vec, &popped) || popped != 999)
        return 4;
    uinx_vec_drop(&vec);

    const char value[] = "abc";
    uinx_arc_raw* first = uinx_arc_new_copy(value, sizeof(value), _Alignof(char));
    if (!first)
        return 5;
    uinx_arc_raw* second = uinx_arc_clone(first);
    if (uinx_arc_strong_count(first) != 2)
        return 6;
    if (strcmp((const char*)uinx_arc_data(first), "abc"))
        return 7;
    uinx_arc_release(second);
    uinx_arc_release(first);

    uinx_hashmap_raw* map = uinx_hashmap_new();
    if (!map)
        return 8;
    for (uintptr_t i = 0; i < 512; ++i) {
        char key[32];
        size_t len = (size_t)snprintf(key, sizeof(key), "key-%llu", (unsigned long long)i);
        if (uinx_hashmap_put(map, key, len, i + 100))
            return 9;
    }
    if (uinx_hashmap_len(map) != 512)
        return 10;
    uintptr_t out = 0;
    if (uinx_hashmap_get(map, "key-42", 6, &out) || out != 142)
        return 11;
    if (uinx_hashmap_remove(map, "key-42", 6, &out) || out != 142)
        return 12;
    if (uinx_hashmap_len(map) != 511)
        return 13;
    uinx_hashmap_drop(map);

    const uint8_t utf8[] = {0xE4, 0xB8, 0xAD};
    if (uinx_utf8_validate(utf8, sizeof(utf8)))
        return 14;
    size_t offset = 0;
    uint32_t codepoint = 0;
    if (uinx_utf8_next(utf8, sizeof(utf8), &offset, &codepoint) || codepoint != 0x4E2D ||
        offset != 3)
        return 15;
    uint8_t encoded[4] = {0};
    size_t encoded_len = 0;
    if (uinx_utf8_encode(codepoint, encoded, &encoded_len) || encoded_len != 3 ||
        memcmp(encoded, utf8, 3))
        return 16;
    const uint8_t bad[] = {0xC0, 0xAF};
    if (!uinx_utf8_validate(bad, sizeof(bad)))
        return 17;

    uint64_t atom = 1;
    if (uinx_atomic_fetch_add_u64(&atom, 2, 4) != 1 || uinx_atomic_load_u64(&atom, 4) != 3)
        return 18;
    uint64_t expected = 3;
    if (!uinx_atomic_compare_exchange_u64(&atom, &expected, 9, 4, 4) || atom != 9)
        return 19;

    uinx_mutex* mutex = uinx_mutex_new();
    if (!mutex)
        return 20;
#if !defined(_WIN32)
    if (uinx_mutex_lock(mutex) || uinx_mutex_unlock(mutex))
        return 21;
#endif
    uinx_mutex_drop(mutex);

    uint64_t thread_value = 1;
    uinx_thread_handle handle = 0;
#if !defined(_WIN32)
    if (uinx_thread_spawn(thread_add, &thread_value, &handle))
        return 22;
    intptr_t thread_result = 0;
    if (uinx_thread_join(handle, &thread_result) || thread_result != 7 || thread_value != 42)
        return 23;
#endif

#if !defined(_WIN32)
    int64_t pipe_fds[2] = {-1, -1};
    if (uinx_pipe_create(pipe_fds))
        return 24;
    const char payload[] = "uinx";
    char readback[sizeof(payload)] = {0};
    if (uinx_file_write(pipe_fds[1], payload, sizeof(payload)) != (int64_t)sizeof(payload))
        return 25;
    if (uinx_file_read(pipe_fds[0], readback, sizeof(readback)) != (int64_t)sizeof(readback))
        return 26;
    if (memcmp(payload, readback, sizeof(payload)))
        return 27;
    if (uinx_file_close(pipe_fds[0]) || uinx_file_close(pipe_fds[1]))
        return 28;

    char* argv[] = {(char*)"sh", (char*)"-c", (char*)"exit 7", NULL};
    int64_t pid = -1;
    if (uinx_process_spawn("/bin/sh", argv, &pid))
        return 29;
    int32_t status = 0;
    if (uinx_process_wait(pid, &status))
        return 30;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 7)
        return 31;
#endif

    uint32_t volatile_value = 5;
    uinx_volatile_store_u32(&volatile_value, 42);
    if (uinx_volatile_load_u32(&volatile_value) != 42)
        return 32;

    if (uinx_time_monotonic_ns() == 0)
        return 33;
    return 0;
}
