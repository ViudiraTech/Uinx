/* SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 ViudiraTech
// By JiTianYu391
 */

.section .text.boot,"ax",@progbits
.global _start
.type _start,%function
_start:
    bl kernel_main
1:
    wfe
    b 1b
.size _start, .-_start
