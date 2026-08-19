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
.type _start,@function
_start:
    la sp, __boot_stack_top
    call kernel_main
1:
    wfi
    j 1b
.size _start, .-_start
