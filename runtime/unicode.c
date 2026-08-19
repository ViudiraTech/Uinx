// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391

#include "runtime.h"
#include <errno.h>
int32_t uinx_utf8_next(const uint8_t*d,size_t n,size_t*off,uint32_t*cp){if(!d||!off||!cp||*off>=n)return -EINVAL;size_t i=*off;uint8_t a=d[i++];uint32_t v;size_t need;if(a<0x80){v=a;need=0;}else if((a&0xE0)==0xC0){v=a&0x1F;need=1;if(v<2)return -EILSEQ;}else if((a&0xF0)==0xE0){v=a&0x0F;need=2;}else if((a&0xF8)==0xF0){v=a&0x07;need=3;if(v>4)return -EILSEQ;}else return -EILSEQ;if(i+need>n)return -EILSEQ;for(size_t j=0;j<need;++j){uint8_t b=d[i++];if((b&0xC0)!=0x80)return -EILSEQ;v=(v<<6)|(b&0x3F);}if((need==2&&v<0x800)||(need==3&&v<0x10000)||v>0x10FFFF||(v>=0xD800&&v<=0xDFFF))return -EILSEQ;*off=i;*cp=v;return 0;}
int32_t uinx_utf8_validate(const uint8_t*d,size_t n){size_t o=0;uint32_t c;while(o<n)if(uinx_utf8_next(d,n,&o,&c))return -EILSEQ;return 0;}
int32_t uinx_utf8_encode(uint32_t c,uint8_t out[4],size_t*n){if(!out||!n||c>0x10FFFF||(c>=0xD800&&c<=0xDFFF))return -EINVAL;if(c<0x80){out[0]=(uint8_t)c;*n=1;}else if(c<0x800){out[0]=(uint8_t)(0xC0|(c>>6));out[1]=(uint8_t)(0x80|(c&0x3F));*n=2;}else if(c<0x10000){out[0]=(uint8_t)(0xE0|(c>>12));out[1]=(uint8_t)(0x80|((c>>6)&0x3F));out[2]=(uint8_t)(0x80|(c&0x3F));*n=3;}else{out[0]=(uint8_t)(0xF0|(c>>18));out[1]=(uint8_t)(0x80|((c>>12)&0x3F));out[2]=(uint8_t)(0x80|((c>>6)&0x3F));out[3]=(uint8_t)(0x80|(c&0x3F));*n=4;}return 0;}
