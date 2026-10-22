/* nld1 の -shared の試験の台本。Linux 5.4 の arch/riscv/kernel/vdso/vdso.lds.S
 * を前処理したものと同じ形 (PHDRS・FILEHDR・:phdr の引継ぎ・VERSION) */
OUTPUT_ARCH(riscv)
SECTIONS
{
 . = SIZEOF_HEADERS;
 .hash : { *(.hash) } :text
 .gnu.hash : { *(.gnu.hash) }
 .dynsym : { *(.dynsym) }
 .dynstr : { *(.dynstr) }
 .gnu.version : { *(.gnu.version) }
 .gnu.version_d : { *(.gnu.version_d) }
 .gnu.version_r : { *(.gnu.version_r) }
 .note : { *(.note.*) } :text :note
 .dynamic : { *(.dynamic) } :text :dynamic
 .eh_frame_hdr : { *(.eh_frame_hdr) } :text :eh_frame_hdr
 .eh_frame : { KEEP (*(.eh_frame)) } :text
 .rodata : { *(.rodata .rodata.* .gnu.linkonce.r.*) }
 . = 0x800;
 .text : { *(.text .text.*) } :text
 .data : {
  *(.got.plt) *(.got)
  *(.data .data.* .gnu.linkonce.d.*)
  *(.dynbss)
  *(.bss .bss.* .gnu.linkonce.b.*)
 }
}
PHDRS
{
 text PT_LOAD FLAGS(5) FILEHDR PHDRS;
 dynamic PT_DYNAMIC FLAGS(4);
 note PT_NOTE FLAGS(4);
 eh_frame_hdr PT_GNU_EH_FRAME;
}
VERSION
{
 TEST_1.0 {
 global:
  dso_alpha;
  dso_beta;
  dso_gamma;
 local: *;
 };
}
