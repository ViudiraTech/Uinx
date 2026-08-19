/* SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391
 */

.section .text.boot,"ax",@progbits
.global _start
.type _start,@function
_start:
    call kernel_main
1:
    hlt
    jmp 1b
.size _start, .-_start
