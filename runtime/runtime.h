// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#ifndef UINX_RUNTIME_H
#define UINX_RUNTIME_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef void* (*uinx_alloc_fn)(size_t,size_t,void*);
typedef void* (*uinx_realloc_fn)(void*,size_t,size_t,size_t,void*);
typedef void (*uinx_free_fn)(void*,size_t,size_t,void*);
typedef struct { uinx_alloc_fn alloc; uinx_realloc_fn realloc; uinx_free_fn free; void* context; } uinx_allocator;
void uinx_set_allocator(uinx_allocator allocator);
void* uinx_alloc(size_t size,size_t align);
void* uinx_realloc(void* ptr,size_t old_size,size_t new_size,size_t align);
void uinx_dealloc(void* ptr,size_t size,size_t align);
void uinx_panic(const char* message,const char* file,uint32_t line);
uint64_t uinx_time_monotonic_ns(void);
int64_t uinx_file_open(const char* path,int32_t flags,int32_t mode);
int64_t uinx_file_read(int64_t fd,void* buf,uint64_t len);
int64_t uinx_file_write(int64_t fd,const void* buf,uint64_t len);
int32_t uinx_file_close(int64_t fd);
int64_t uinx_socket(int32_t domain,int32_t type,int32_t protocol);
int32_t uinx_socket_connect(int64_t fd,const void* addr,uint32_t len);
int64_t uinx_socket_send(int64_t fd,const void* buf,uint64_t len,int32_t flags);
int64_t uinx_socket_recv(int64_t fd,void* buf,uint64_t len,int32_t flags);
typedef uintptr_t uinx_thread_handle;
typedef intptr_t (*uinx_thread_fn)(void*);
int32_t uinx_thread_spawn(uinx_thread_fn fn,void* arg,uinx_thread_handle* out);
int32_t uinx_thread_join(uinx_thread_handle handle,intptr_t* result);
void uinx_yield(void);
typedef int32_t (*uinx_poll_fn)(void* state,void* output);
typedef void (*uinx_future_drop_fn)(void* state);
typedef struct { uinx_poll_fn poll; uinx_future_drop_fn drop; } uinx_future_vtable;
typedef struct { void* state; const uinx_future_vtable* vtable; } uinx_future_raw;
int32_t uinx_block_on(uinx_poll_fn poll,void* state,void* output);
uinx_future_raw uinx_yield_once_i32(int32_t value);

/* Raw allocation-backed building blocks used by alloc/std. */
typedef struct { void* data; size_t len; size_t cap; size_t elem_size; size_t elem_align; } uinx_vec_raw;
int32_t uinx_vec_init(uinx_vec_raw* vec,size_t elem_size,size_t elem_align);
int32_t uinx_vec_reserve(uinx_vec_raw* vec,size_t additional);
int32_t uinx_vec_push(uinx_vec_raw* vec,const void* elem);
int32_t uinx_vec_pop(uinx_vec_raw* vec,void* out_elem);
void uinx_vec_clear(uinx_vec_raw* vec);
void uinx_vec_drop(uinx_vec_raw* vec);

typedef struct uinx_arc_raw uinx_arc_raw;
uinx_arc_raw* uinx_arc_new_copy(const void* value,size_t size,size_t align);
uinx_arc_raw* uinx_arc_clone(uinx_arc_raw* arc);
void* uinx_arc_data(uinx_arc_raw* arc);
uint64_t uinx_arc_strong_count(const uinx_arc_raw* arc);
void uinx_arc_release(uinx_arc_raw* arc);

typedef struct uinx_hashmap_raw uinx_hashmap_raw;
uinx_hashmap_raw* uinx_hashmap_new(void);
int32_t uinx_hashmap_put(uinx_hashmap_raw* map,const void* key,size_t key_len,uintptr_t value);
int32_t uinx_hashmap_get(const uinx_hashmap_raw* map,const void* key,size_t key_len,uintptr_t* out_value);
int32_t uinx_hashmap_remove(uinx_hashmap_raw* map,const void* key,size_t key_len,uintptr_t* out_value);
size_t uinx_hashmap_len(const uinx_hashmap_raw* map);
void uinx_hashmap_drop(uinx_hashmap_raw* map);

int32_t uinx_utf8_validate(const uint8_t* data,size_t len);
int32_t uinx_utf8_next(const uint8_t* data,size_t len,size_t* offset,uint32_t* codepoint);
int32_t uinx_utf8_encode(uint32_t codepoint,uint8_t out[4],size_t* out_len);


uint64_t uinx_atomic_load_u64(const uint64_t* ptr,int32_t order);
void uinx_atomic_store_u64(uint64_t* ptr,uint64_t value,int32_t order);
uint64_t uinx_atomic_fetch_add_u64(uint64_t* ptr,uint64_t value,int32_t order);
int32_t uinx_atomic_compare_exchange_u64(uint64_t* ptr,uint64_t* expected,uint64_t desired,int32_t success_order,int32_t failure_order);

typedef struct uinx_mutex uinx_mutex;
typedef struct uinx_condvar uinx_condvar;
uinx_mutex* uinx_mutex_new(void);
int32_t uinx_mutex_lock(uinx_mutex* mutex);
int32_t uinx_mutex_unlock(uinx_mutex* mutex);
void uinx_mutex_drop(uinx_mutex* mutex);
uinx_condvar* uinx_condvar_new(void);
int32_t uinx_condvar_wait(uinx_condvar* cond,uinx_mutex* mutex);
int32_t uinx_condvar_signal(uinx_condvar* cond);
int32_t uinx_condvar_broadcast(uinx_condvar* cond);
void uinx_condvar_drop(uinx_condvar* cond);

int32_t uinx_process_spawn(const char* path,char* const argv[],int64_t* out_pid);
int32_t uinx_process_wait(int64_t pid,int32_t* out_status);
int32_t uinx_pipe_create(int64_t out_fds[2]);

void* uinx_memcpy(void* dst,const void* src,size_t len);
void* uinx_memmove(void* dst,const void* src,size_t len);
void* uinx_memset(void* dst,int value,size_t len);
uint8_t uinx_volatile_load_u8(const volatile uint8_t* ptr);
uint32_t uinx_volatile_load_u32(const volatile uint32_t* ptr);
uint64_t uinx_volatile_load_u64(const volatile uint64_t* ptr);
void uinx_volatile_store_u8(volatile uint8_t* ptr,uint8_t value);
void uinx_volatile_store_u32(volatile uint32_t* ptr,uint32_t value);
void uinx_volatile_store_u64(volatile uint64_t* ptr,uint64_t value);

#ifdef __cplusplus
}
#endif
#endif
