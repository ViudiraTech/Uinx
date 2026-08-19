/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 ViudiraTech
 * By JiTianYu391
 */

.section .bss.stack,"aw",@nobits
.align 16
__boot_stack:
    .skip 16384
__boot_stack_top:

.section .text.boot,"ax",@progbits
.global _start
.type _start,@function
_start:
    lea __boot_stack_top(%rip), %rsp
    xor %rbp, %rbp
    call kernel_main
1:
    hlt
    jmp 1b
.size _start, .-_start
