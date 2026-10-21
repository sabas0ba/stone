/* init1.lds --- init の台本 (docs/stage018-ncc.md 17 章)
 *
 * 利用者の空間の 0x10000 から置く。Linux の ELF の読込みは PT_LOAD の
 * ファイルの位置と番地が 4 KiB を法として揃っていることを要るので，
 * PHDRS で ELF の頭を含む命令の段と，書込みのできる段を分ける */
ENTRY(_start)
SECTIONS
{
  . = 0x10000 + SIZEOF_HEADERS;
  .text : { *(.text.init) *(.text .text.*) } :text
  .rodata : { *(.rodata .rodata.* .srodata .srodata.*) }
  . = ALIGN(0x1000);
  .data : { *(.data .data.* .sdata .sdata.*) } :data
  .bss : { *(.sbss .sbss.* .bss .bss.*) *(COMMON) }
}
PHDRS
{
  text PT_LOAD FILEHDR PHDRS FLAGS(5);
  data PT_LOAD FLAGS(6);
}
