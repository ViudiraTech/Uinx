/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 ViudiraTech
 * By JiTianYu391
 */

.section .bss.stack,"aw",@nobits
.align 12
__boot_stack:
    .skip 16384
__boot_stack_top:

.section .text.boot,"ax",@progbits
.global _start
.type _start,%function
_start:
    adrp x0, __boot_stack_top
    add x0, x0, :lo12:__boot_stack_top
    mov sp, x0
    bl kernel_main
1:
    wfe
    b 1b
.size _start, .-_start
