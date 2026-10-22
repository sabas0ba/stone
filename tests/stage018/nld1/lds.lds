/* nld1 のリンカ台本の試験 (tests/stage018/nld1/lds.s と組にする) */
OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS
{
  . = 0x1000;
  .text : {
    *(.text)
    *(SORT(.text.*))
  }
  . = ALIGN(0x100);
  .rodata : { *(.rodata) BYTE(0x7f) SHORT(0x1234) LONG(0xdeadbeef) }
  .data 0x2000 : AT(0x3000) { *(.data) }
  .init.data : {
    __init_start = .;
    KEEP(*(.init.x))
    __init_end = .;
  }
  /DISCARD/ : { *(.discard.*) }
  .bss : { *(.bss) }
  PROVIDE(prov_sym = 0x1234);
  PROVIDE(prov_unused = 0x5678);
  text_size = SIZEOF(.text);
  data_lma = LOADADDR(.data);
  init_lma = LOADADDR(.init.data);
  rodata_addr = ADDR(.rodata);
  ASSERT(SIZEOF(.text) == 16, "text size")
}
