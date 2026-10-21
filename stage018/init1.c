/* init1.c --- initramfs の init (docs/stage018-ncc.md 17 章)
 *
 * Linux が最初に走らせる利用者のプログラム。ncc1・nas1・nld1 で組む静的な
 * ELF で，libc を使わず syscall を ecall で直に呼ぶ (rv32 の asm-generic の
 * 番号)。syscall が期待どおりに返ることを確かめて結果を console に書き，
 * reboot(POWER_OFF) で電源を切る。カーネルは SBI の shutdown を呼び，
 * QEMU が止まる。
 *
 *   getpid    1 (最初の利用者のプロセス)
 *   uname     sysname が "Linux"，machine が "riscv32"
 *   write     console (fd 1) に書いた長さが返る
 *   openat / read / close   /init 自身を開いて ELF の頭を読む
 */

#define SYS_openat 56
#define SYS_close 57
#define SYS_read 63
#define SYS_write 64
#define SYS_exit 93
#define SYS_reboot 142
#define SYS_uname 160
#define SYS_getpid 172

#define AT_FDCWD -100
#define LINUX_REBOOT_MAGIC1 0xfee1deadUL
#define LINUX_REBOOT_MAGIC2 672274793UL
#define LINUX_REBOOT_CMD_POWER_OFF 0x4321fedcUL

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

static long syscall4(long n, long a, long b, long c, long d)
{
    register long a0 __asm__("a0") = a;
    register long a1 __asm__("a1") = b;
    register long a2 __asm__("a2") = c;
    register long a3 __asm__("a3") = d;
    register long a7 __asm__("a7") = n;
    __asm__ __volatile__("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a3), "r"(a7) : "memory");
    return a0;
}

static long len(const char *s)
{
    long n = 0;
    while (s[n])
        n++;
    return n;
}

static long say(const char *s)
{
    return syscall4(SYS_write, 1, (long)s, len(s), 0);
}

static void saynum(long v)
{
    char b[24];
    int i = 23;
    unsigned long u = v < 0 ? (unsigned long)-v : (unsigned long)v;
    b[i] = 0;
    do {
        b[--i] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    if (v < 0)
        b[--i] = '-';
    say(b + i);
}

static int same(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int failures;

static void check(const char *what, int ok)
{
    say(ok ? "init: ok   " : "init: FAIL ");
    say(what);
    say("\n");
    if (!ok)
        failures++;
}

int main(void)
{
    struct utsname u;
    long pid, n, fd;
    unsigned char head[4];

    n = say("init: stone の init が走った\n");
    check("write が書いた長さを返す", n == len("init: stone の init が走った\n"));
    pid = syscall4(SYS_getpid, 0, 0, 0, 0);
    say("init: pid ");
    saynum(pid);
    say("\n");
    check("getpid が 1", pid == 1);
    n = syscall4(SYS_uname, (long)&u, 0, 0, 0);
    check("uname が 0 を返す", n == 0);
    say("init: ");
    say(u.sysname);
    say(" ");
    say(u.release);
    say(" ");
    say(u.machine);
    say("\n");
    check("sysname が Linux", same(u.sysname, "Linux"));
    check("machine が riscv32", same(u.machine, "riscv32"));
    fd = syscall4(SYS_openat, AT_FDCWD, (long)"/init", 0, 0);
    check("openat(/init) が fd を返す", fd >= 0);
    n = syscall4(SYS_read, fd, (long)head, 4, 0);
    check("read が ELF の頭を読む", n == 4 && head[0] == 0x7f && head[1] == 'E' && head[2] == 'L'
                                        && head[3] == 'F');
    check("close が 0 を返す", syscall4(SYS_close, fd, 0, 0, 0) == 0);
    say(failures ? "init: 失敗があった\n" : "init: すべて期待どおり。電源を切る\n");
    if (failures)
        syscall4(SYS_exit, 1, 0, 0, 0);
    syscall4(SYS_reboot, (long)LINUX_REBOOT_MAGIC1, (long)LINUX_REBOOT_MAGIC2,
             (long)LINUX_REBOOT_CMD_POWER_OFF, 0);
    say("init: reboot から戻った\n");
    return 1;
}
