/* sbi1.c --- 最小の SBI (docs/stage018-ncc.md 17 章)
 *
 * QEMU virt (rv32・1 hart) の M モードで走り，Linux 5.4 が呼ぶ SBI v0.1 の
 * 呼出し (legacy extension) を受ける。OpenSBI は使わない
 * (docs/stage019-linux.md 2 章)。仕様は RISC-V SBI specification の
 * legacy extension の章と，privileged architecture の M モードの CSR に従う。
 *
 *   0 set_timer          CLINT の mtimecmp に書き，S の timer 割込みを下げる
 *   1 console_putchar    UART (16550) に 1 文字書く
 *   2 console_getchar    UART から 1 文字読む (無ければ -1)
 *   3 clear_ipi          S のソフトウェア割込みを下げる
 *   4 send_ipi           1 hart なので自分にだけ上げる
 *   5 remote_fence_i     fence.i
 *   6/7 remote_sfence_vma[_asid]   sfence.vma
 *   8 shutdown           test finisher に書いて QEMU を止める
 *
 * M の timer 割込みは S の timer 割込み (mip.STIP) に移して M では止める。
 * 例外と S の割込みは S へ委ねる (medeleg・mideleg)。それ以外の trap は
 * 内容を出して異常の終了コードで止める。
 *
 * ncc1 で訳す。libc は使わない。
 */

#define UART_BASE 0x10000000UL
#define CLINT_MTIMECMP 0x2004000UL
#define TEST_FINISHER 0x100000UL
#define FW_DYNAMIC_MAGIC 0x4942534fUL   /* "OSBI" */
#define DEFAULT_NEXT 0x80400000UL

#define MIP_SSIP 0x2UL
#define MIP_STIP 0x20UL
#define MIE_MTIE 0x80UL

/* QEMU が a2 で渡す次の段の情報 (OpenSBI の fw_dynamic と同じ形) */
struct fw_dynamic_info {
    unsigned long magic;
    unsigned long version;
    unsigned long next_addr;
    unsigned long next_mode;
    unsigned long options;
    unsigned long boot_hart;
};

void sbi_trap_entry(void);

static void putc_raw(int c)
{
    volatile unsigned char *u = (volatile unsigned char *)UART_BASE;
    while (!(u[5] & 0x20))
        ;
    u[0] = (unsigned char)c;
}

static int getc_raw(void)
{
    volatile unsigned char *u = (volatile unsigned char *)UART_BASE;
    if (u[5] & 1)
        return u[0];
    return -1;
}

static void puts_raw(const char *s)
{
    while (*s)
        putc_raw(*s++);
}

static void puthex(unsigned long v)
{
    int i;
    puts_raw("0x");
    for (i = 28; i >= 0; i -= 4)
        putc_raw("0123456789abcdef"[(v >> i) & 15]);
}

/* QEMU を止める。0 なら正常 (0x5555)，それ以外は (code << 16) | 0x3333 */
static void finish(unsigned long code)
{
    volatile unsigned long *t = (volatile unsigned long *)TEST_FINISHER;
    *t = code ? (code << 16) | 0x3333UL : 0x5555UL;
    for (;;)
        ;
}

static void fail(const char *msg, unsigned long cause, unsigned long epc, unsigned long tval)
{
    puts_raw("\nstone sbi: ");
    puts_raw(msg);
    puts_raw(" mcause=");
    puthex(cause);
    puts_raw(" mepc=");
    puthex(epc);
    puts_raw(" mtval=");
    puthex(tval);
    puts_raw("\n");
    finish(2);
}

unsigned long sbi_main(unsigned long hartid, unsigned long dtb, struct fw_dynamic_info *info)
{
    unsigned long next = DEFAULT_NEXT, st;
    if (info && info->magic == FW_DYNAMIC_MAGIC)
        next = info->next_addr;
    puts_raw("stone sbi: hart ");
    puthex(hartid);
    puts_raw(" dtb ");
    puthex(dtb);
    puts_raw(" next ");
    puthex(next);
    puts_raw("\n");
    __asm__ __volatile__("csrw mtvec, %0" : : "r"(sbi_trap_entry));
    /* 例外: 命令の番地の不整列 (0)・breakpoint (3)・U の ecall (8)・
     * ページフォールト (12・13・15) を S へ */
    __asm__ __volatile__("csrw medeleg, %0" : : "r"(0xb109UL));
    /* 割込み: S のソフトウェア・timer・外部 */
    __asm__ __volatile__("csrw mideleg, %0" : : "r"(0x222UL));
    /* S と U から cycle・time・instret を読める */
    __asm__ __volatile__("csrw mcounteren, %0" : : "r"(7UL));
    /* PMP: 全体を S と U に開く (NAPOT，RWX) */
    __asm__ __volatile__("csrw pmpaddr0, %0" : : "r"(0xffffffffUL));
    __asm__ __volatile__("csrw pmpcfg0, %0" : : "r"(0x1fUL));
    __asm__ __volatile__("csrw mie, zero");
    __asm__ __volatile__("csrw satp, zero");
    /* mret で S へ: MPP = 01，M と S の割込みの許可は落とす */
    __asm__ __volatile__("csrr %0, mstatus" : "=r"(st));
    st &= ~(0x1800UL | 0x8UL | 0x2UL | 0x80UL);
    st |= 0x800UL;
    __asm__ __volatile__("csrw mstatus, %0" : : "r"(st));
    return next;
}

static void set_timer(unsigned long lo, unsigned long hi)
{
    volatile unsigned long *cmp = (volatile unsigned long *)CLINT_MTIMECMP;
    /* 上位を先に最大にして，途中の値で割込みが起きないようにする */
    cmp[1] = 0xffffffffUL;
    cmp[0] = lo;
    cmp[1] = hi;
    __asm__ __volatile__("csrc mip, %0" : : "r"(MIP_STIP));
    __asm__ __volatile__("csrs mie, %0" : : "r"(MIE_MTIE));
}

/* SBI の呼出し。r は x0〜x31 の枠 (a0 = r[10]，a7 = r[17]) */
static void sbi_call(unsigned long *r)
{
    switch (r[17]) {
    case 0:
        set_timer(r[10], r[11]);
        r[10] = 0;
        break;
    case 1:
        putc_raw((int)(r[10] & 0xff));
        r[10] = 0;
        break;
    case 2:
        r[10] = (unsigned long)getc_raw();
        break;
    case 3:
        __asm__ __volatile__("csrc mip, %0" : : "r"(MIP_SSIP));
        r[10] = 0;
        break;
    case 4:
        __asm__ __volatile__("csrs mip, %0" : : "r"(MIP_SSIP));
        r[10] = 0;
        break;
    case 5:
        __asm__ __volatile__("fence.i");
        r[10] = 0;
        break;
    case 6:
    case 7:
        __asm__ __volatile__("sfence.vma");
        r[10] = 0;
        break;
    case 8:
        puts_raw("stone sbi: shutdown\n");
        finish(0);
        break;
    default:
        /* SBI_ERR_NOT_SUPPORTED */
        r[10] = (unsigned long)-2L;
        r[11] = 0;
        break;
    }
}

void sbi_trap(unsigned long *r)
{
    unsigned long cause, epc, tval;
    __asm__ __volatile__("csrr %0, mcause" : "=r"(cause));
    __asm__ __volatile__("csrr %0, mepc" : "=r"(epc));
    __asm__ __volatile__("csrr %0, mtval" : "=r"(tval));
    if (cause & 0x80000000UL) {
        if ((cause & 0xff) == 7) {
            /* M の timer: S の timer として上げ，M では止める */
            __asm__ __volatile__("csrs mip, %0" : : "r"(MIP_STIP));
            __asm__ __volatile__("csrc mie, %0" : : "r"(MIE_MTIE));
            return;
        }
        fail("予期しない割込み", cause, epc, tval);
    }
    if (cause == 9) {
        sbi_call(r);
        epc += 4;
        __asm__ __volatile__("csrw mepc, %0" : : "r"(epc));
        return;
    }
    fail("扱わない trap", cause, epc, tval);
}
