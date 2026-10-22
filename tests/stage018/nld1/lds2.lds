/* 前方の参照が 5 段の鎖 (a は e が決まるまで決まらない)，SECTIONS 直下の
 * 複合代入のすべて，値が 0 の入口 */
ENTRY(zero)
SECTIONS
{
  zero = 0;
  . = 0x4000;
  .data : { *(.data) LONG(a) LONG(x) }
  a = b; b = c; c = d; d = e; e = 5;
  x = 1; x <<= 3; x %= 5; x ^= 6; x >>= 1; x += 0x10; x *= 2; x -= 1; x /= 3;
  x &= 0xff; x |= 0x100;
}
