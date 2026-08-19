// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "runtime.h"
#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static int mul_overflow_size(size_t a,size_t b,size_t* out){if(a&&b>SIZE_MAX/a)return 1;*out=a*b;return 0;}
int32_t uinx_vec_init(uinx_vec_raw* v,size_t size,size_t align){if(!v||size==0||align==0||(align&(align-1)))return -EINVAL;v->data=NULL;v->len=0;v->cap=0;v->elem_size=size;v->elem_align=align;return 0;}
int32_t uinx_vec_reserve(uinx_vec_raw* v,size_t add){if(!v)return -EINVAL;if(add>SIZE_MAX-v->len)return -EOVERFLOW;size_t need=v->len+add;if(need<=v->cap)return 0;size_t cap=v->cap?v->cap:8;while(cap<need){if(cap>SIZE_MAX/2){cap=need;break;}cap*=2;}size_t old_bytes,new_bytes;if(mul_overflow_size(v->cap,v->elem_size,&old_bytes)||mul_overflow_size(cap,v->elem_size,&new_bytes))return -EOVERFLOW;void*p=uinx_realloc(v->data,old_bytes,new_bytes,v->elem_align);if(!p)return -ENOMEM;v->data=p;v->cap=cap;return 0;}
int32_t uinx_vec_push(uinx_vec_raw* v,const void* elem){if(!v||!elem)return -EINVAL;int32_t rc=uinx_vec_reserve(v,1);if(rc)return rc;memcpy((unsigned char*)v->data+v->len*v->elem_size,elem,v->elem_size);++v->len;return 0;}
int32_t uinx_vec_pop(uinx_vec_raw* v,void* out){if(!v||v->len==0)return -ENOENT;--v->len;if(out)memcpy(out,(unsigned char*)v->data+v->len*v->elem_size,v->elem_size);return 0;}
void uinx_vec_clear(uinx_vec_raw* v){if(v)v->len=0;}
void uinx_vec_drop(uinx_vec_raw* v){if(!v)return;size_t bytes=0;(void)mul_overflow_size(v->cap,v->elem_size,&bytes);uinx_dealloc(v->data,bytes,v->elem_align);v->data=NULL;v->len=v->cap=0;}

struct uinx_arc_raw{atomic_uint_fast64_t refs;void* data;size_t size;size_t align;};
uinx_arc_raw* uinx_arc_new_copy(const void* value,size_t size,size_t align){if(!value||!size||!align)return NULL;uinx_arc_raw*a=(uinx_arc_raw*)uinx_alloc(sizeof(*a),_Alignof(uinx_arc_raw));if(!a)return NULL;a->data=uinx_alloc(size,align);if(!a->data){uinx_dealloc(a,sizeof(*a),_Alignof(uinx_arc_raw));return NULL;}memcpy(a->data,value,size);a->size=size;a->align=align;atomic_init(&a->refs,1);return a;}
uinx_arc_raw* uinx_arc_clone(uinx_arc_raw*a){if(!a)return NULL;atomic_fetch_add_explicit(&a->refs,1,memory_order_relaxed);return a;}
void* uinx_arc_data(uinx_arc_raw*a){return a?a->data:NULL;}
uint64_t uinx_arc_strong_count(const uinx_arc_raw*a){return a?atomic_load_explicit(&a->refs,memory_order_acquire):0;}
void uinx_arc_release(uinx_arc_raw*a){if(!a)return;if(atomic_fetch_sub_explicit(&a->refs,1,memory_order_acq_rel)==1){uinx_dealloc(a->data,a->size,a->align);uinx_dealloc(a,sizeof(*a),_Alignof(uinx_arc_raw));}}

typedef struct{uint64_t hash;unsigned char*key;size_t key_len;uintptr_t value;unsigned char state;} entry;
struct uinx_hashmap_raw{entry*entries;size_t cap;size_t len;size_t tombs;};
static uint64_t hash_bytes(const void*data,size_t len){const unsigned char*p=(const unsigned char*)data;uint64_t h=1469598103934665603ull;for(size_t i=0;i<len;++i){h^=p[i];h*=1099511628211ull;}return h?h:1;}
static int key_eq(const entry*e,const void*k,size_t n,uint64_t h){return e->state==1&&e->hash==h&&e->key_len==n&&memcmp(e->key,k,n)==0;}
static int map_resize(uinx_hashmap_raw*m,size_t cap){entry*ne=(entry*)calloc(cap,sizeof(entry));if(!ne)return -ENOMEM;entry*old=m->entries;size_t oldcap=m->cap;m->entries=ne;m->cap=cap;m->len=0;m->tombs=0;for(size_t i=0;i<oldcap;++i)if(old[i].state==1){size_t idx=(size_t)(old[i].hash&(cap-1));while(ne[idx].state==1)idx=(idx+1)&(cap-1);ne[idx]=old[i];++m->len;}free(old);return 0;}
uinx_hashmap_raw* uinx_hashmap_new(void){uinx_hashmap_raw*m=(uinx_hashmap_raw*)calloc(1,sizeof(*m));if(!m)return NULL;if(map_resize(m,16)){free(m);return NULL;}return m;}
int32_t uinx_hashmap_put(uinx_hashmap_raw*m,const void*k,size_t n,uintptr_t v){if(!m||(!k&&n))return -EINVAL;if((m->len+m->tombs+1)*10>=m->cap*7){int rc=map_resize(m,m->cap*2);if(rc)return rc;}uint64_t h=hash_bytes(k,n);size_t idx=(size_t)(h&(m->cap-1)),tomb=SIZE_MAX;for(;;){entry*e=&m->entries[idx];if(e->state==0){if(tomb!=SIZE_MAX)e=&m->entries[tomb];e->key=(unsigned char*)malloc(n?n:1);if(!e->key)return -ENOMEM;if(n)memcpy(e->key,k,n);e->key_len=n;e->hash=h;e->value=v;e->state=1;++m->len;if(tomb!=SIZE_MAX)--m->tombs;return 0;}if(e->state==2&&tomb==SIZE_MAX)tomb=idx;else if(key_eq(e,k,n,h)){e->value=v;return 0;}idx=(idx+1)&(m->cap-1);}}
int32_t uinx_hashmap_get(const uinx_hashmap_raw*m,const void*k,size_t n,uintptr_t*out){if(!m||(!k&&n))return -EINVAL;uint64_t h=hash_bytes(k,n);size_t idx=(size_t)(h&(m->cap-1));for(;;){const entry*e=&m->entries[idx];if(e->state==0)return -ENOENT;if(key_eq(e,k,n,h)){if(out)*out=e->value;return 0;}idx=(idx+1)&(m->cap-1);}}
int32_t uinx_hashmap_remove(uinx_hashmap_raw*m,const void*k,size_t n,uintptr_t*out){if(!m)return -EINVAL;uint64_t h=hash_bytes(k,n);size_t idx=(size_t)(h&(m->cap-1));for(;;){entry*e=&m->entries[idx];if(e->state==0)return -ENOENT;if(key_eq(e,k,n,h)){if(out)*out=e->value;free(e->key);e->key=NULL;e->state=2;--m->len;++m->tombs;return 0;}idx=(idx+1)&(m->cap-1);}}
size_t uinx_hashmap_len(const uinx_hashmap_raw*m){return m?m->len:0;}
void uinx_hashmap_drop(uinx_hashmap_raw*m){if(!m)return;for(size_t i=0;i<m->cap;++i)if(m->entries[i].state==1)free(m->entries[i].key);free(m->entries);free(m);}
