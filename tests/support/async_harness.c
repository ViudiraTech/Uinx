// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "runtime.h"
#include <stdint.h>
#include <stdio.h>

typedef struct { void* state; const uinx_future_vtable* vtable; } uinx_future_abi;
extern uinx_future_abi async_answer(void);

int main(void) {
    uinx_future_abi future = async_answer();
    if (!future.state || !future.vtable || !future.vtable->poll || !future.vtable->drop) return 10;
    int32_t output = -1;
    int32_t first = future.vtable->poll(future.state, &output);
    if (first != 0) {
        fprintf(stderr, "expected first poll Pending(0), got %d\n", first);
        future.vtable->drop(future.state);
        return 11;
    }
    int32_t second = future.vtable->poll(future.state, &output);
    if (second != 1 || output != 42) {
        fprintf(stderr, "expected second poll Ready(42), got rc=%d value=%d\n", second, output);
        future.vtable->drop(future.state);
        return 12;
    }
    future.vtable->drop(future.state);
    return 0;
}
