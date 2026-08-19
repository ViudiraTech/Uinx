/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026 JiTianYu391
 */

ENTRY(_start)
SECTIONS {
  . = 1M;
  .text : ALIGN(16) { KEEP(*(.text.boot)) *(.text .text.*) }
  .rodata : ALIGN(16) { *(.rodata .rodata.*) }
  .data : ALIGN(16) { *(.data .data.*) }
  .bss : ALIGN(16) { *(.bss .bss.* COMMON) }
}
