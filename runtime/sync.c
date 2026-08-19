// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "runtime.h"
#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <pthread.h>
#endif

static memory_order to_order(int32_t o){switch(o){case 0:return memory_order_relaxed;case 1:return memory_order_acquire;case 2:return memory_order_release;case 3:return memory_order_acq_rel;default:return memory_order_seq_cst;}}
uint64_t uinx_atomic_load_u64(const uint64_t*p,int32_t o){return atomic_load_explicit((const _Atomic uint64_t*)p,to_order(o));}
void uinx_atomic_store_u64(uint64_t*p,uint64_t v,int32_t o){atomic_store_explicit((_Atomic uint64_t*)p,v,to_order(o));}
uint64_t uinx_atomic_fetch_add_u64(uint64_t*p,uint64_t v,int32_t o){return atomic_fetch_add_explicit((_Atomic uint64_t*)p,v,to_order(o));}
int32_t uinx_atomic_compare_exchange_u64(uint64_t*p,uint64_t*e,uint64_t d,int32_t so,int32_t fo){return atomic_compare_exchange_strong_explicit((_Atomic uint64_t*)p,e,d,to_order(so),to_order(fo))?1:0;}

struct uinx_mutex {
#if defined(_WIN32)
  int unavailable;
#else
  pthread_mutex_t value;
#endif
};
struct uinx_condvar {
#if defined(_WIN32)
  int unavailable;
#else
  pthread_cond_t value;
#endif
};
uinx_mutex* uinx_mutex_new(void){uinx_mutex*m=(uinx_mutex*)malloc(sizeof(*m));if(!m)return NULL;
#if defined(_WIN32)
  m->unavailable=0;return m;
#else
  if(pthread_mutex_init(&m->value,NULL)){free(m);return NULL;}return m;
#endif
}
int32_t uinx_mutex_lock(uinx_mutex*m){if(!m)return-EINVAL;
#if defined(_WIN32)
  return-ENOSYS;
#else
  int rc=pthread_mutex_lock(&m->value);return rc?-rc:0;
#endif
}
int32_t uinx_mutex_unlock(uinx_mutex*m){if(!m)return-EINVAL;
#if defined(_WIN32)
  return-ENOSYS;
#else
  int rc=pthread_mutex_unlock(&m->value);return rc?-rc:0;
#endif
}
void uinx_mutex_drop(uinx_mutex*m){if(!m)return;
#if !defined(_WIN32)
  pthread_mutex_destroy(&m->value);
#endif
  free(m);
}
uinx_condvar* uinx_condvar_new(void){uinx_condvar*c=(uinx_condvar*)malloc(sizeof(*c));if(!c)return NULL;
#if defined(_WIN32)
  c->unavailable=0;return c;
#else
  if(pthread_cond_init(&c->value,NULL)){free(c);return NULL;}return c;
#endif
}
int32_t uinx_condvar_wait(uinx_condvar*c,uinx_mutex*m){if(!c||!m)return-EINVAL;
#if defined(_WIN32)
  return-ENOSYS;
#else
  int rc=pthread_cond_wait(&c->value,&m->value);return rc?-rc:0;
#endif
}
int32_t uinx_condvar_signal(uinx_condvar*c){if(!c)return-EINVAL;
#if defined(_WIN32)
  return-ENOSYS;
#else
  int rc=pthread_cond_signal(&c->value);return rc?-rc:0;
#endif
}
int32_t uinx_condvar_broadcast(uinx_condvar*c){if(!c)return-EINVAL;
#if defined(_WIN32)
  return-ENOSYS;
#else
  int rc=pthread_cond_broadcast(&c->value);return rc?-rc:0;
#endif
}
void uinx_condvar_drop(uinx_condvar*c){if(!c)return;
#if !defined(_WIN32)
  pthread_cond_destroy(&c->value);
#endif
  free(c);
}
void* uinx_memcpy(void*d,const void*s,size_t n){return memcpy(d,s,n);}void* uinx_memmove(void*d,const void*s,size_t n){return memmove(d,s,n);}void* uinx_memset(void*d,int v,size_t n){return memset(d,v,n);}
uint8_t uinx_volatile_load_u8(const volatile uint8_t*p){return*p;}uint32_t uinx_volatile_load_u32(const volatile uint32_t*p){return*p;}uint64_t uinx_volatile_load_u64(const volatile uint64_t*p){return*p;}
void uinx_volatile_store_u8(volatile uint8_t*p,uint8_t v){*p=v;}void uinx_volatile_store_u32(volatile uint32_t*p,uint32_t v){*p=v;}void uinx_volatile_store_u64(volatile uint64_t*p,uint64_t v){*p=v;}
