#define _DEFAULT_SOURCE

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#define DEFAULT_NLINES (10)
#define MIN_NLINES (5)
#define MAX_NLINES (30)

#define DEFAULT_NCOLS (72)
#define MIN_NCOLS (30)
#define MAX_NCOLS (120)

#define DEFAULT_TABSTOP (8)
#define MIN_TABSTOP (2)
#define MAX_TABSTOP (8)

#define DEFAULT_FILETYPE (UNIX)

#define MARK_RING_SIZE (16)

#define SEARCH_SIZE (100)

#define CMD_MAX (256)

#define CONTINUATION_LINE_STR "\x1b[31m\\\x1b[m"
#define EMPTY_LINE_STR "\x1b[34m~\x1b[m"

#define INFO_PRE ("\x1b[33m")
#define ERROR_PRE ("\x1b[31m\x1b[1m")

#define BUFSIZE (1024 * 1024)

#define BLKSIZE (4096)

#define SCREENBUF_SIZE (MAX_NLINES * (MAX_NCOLS + 1) * 4)

#define guard(cond)             \
        do {                    \
                if (!cond)      \
                        return; \
        } while (0)

#define min(a, b) (((a) < (b)) ? (a) : (b))

enum {
        NEWLINE = 0,
        UTF8 = 1,
};

struct utf8 {
        uint8_t c[4];
};

struct tedchar {
        int kind;
        struct utf8 u;
};

struct tedchar tedchar_newline()
{
        return (struct tedchar){.kind = NEWLINE};
}

struct tedchar tedchar_utf8(struct utf8 u)
{
        return (struct tedchar){
                .kind = UTF8,
                .u = u,
        };
}

bool is_newline(struct tedchar t)
{
        return t.kind == NEWLINE;
}

bool is_tab(struct tedchar t)
{
        return t.kind == UTF8 && t.u.c[0] == '\t';
}

bool is_space(struct tedchar t)
{
        return t.kind == UTF8 && t.u.c[0] == ' ';
}

bool is_whitespace(struct tedchar t)
{
        return is_newline(t) || is_tab(t) || is_space(t);
}

struct utf8 utf8_ascii(char c)
{
        return (struct utf8){.c = {c, 0}};
}

size_t utf8_count(const uint8_t buf[])
{
        switch (buf[0] & 0xf0) {
        case 0xf0:
                return 4;
        case 0xe0:
                return 3;
        case 0xc0:
        case 0xd0:
                return 2;
        default:
                return 1;
        }
}

void utf8_char_copy(uint8_t *dest, uint8_t *src)
{
        switch (utf8_count(src)) {
        case 4:
                dest[3] = src[3];
                [[fallthrough]];
        case 3:
                dest[2] = src[2];
                [[fallthrough]];
        case 2:
                dest[1] = src[1];
                [[fallthrough]];
        default:
                dest[0] = src[0];
        }
}

bool utf8_eq(struct utf8 u1, struct utf8 u2)
{
        size_t n1 = utf8_count(u1.c);
        size_t n2 = utf8_count(u2.c);

        if (n1 != n2)
                return false;

        for (size_t i = 0; i < n1; ++i)
                if (u1.c[i] != u2.c[i])
                        return false;

        return true;
}

enum {
        BEL = 1,
        BS,
        HT,
        LF,
        FF,
        CR,
        ESC,
        CUU,
        CUD,
        CUF,
        CUB,
        CNL,
        CPL,
        CHA,
        CUP,
        CPR,
        ED,
        EL,

        DEL,
        PGUP,
        PGDN,

        F1,
        F2,
        F3,
        F4,
        F5,
        F6,
        F7,
        F8,
        F9,
        F10,
        F11,
        F12,
};

struct key {
        int n, m;
        struct utf8 u;
        int shift, ctrl, meta, super;
        int special;
};

bool key_eq(struct key k1, struct key k2)
{
        if (k1.shift != k2.shift)
                return false;
        if (k1.ctrl != k2.ctrl)
                return false;
        if (k1.meta != k2.meta)
                return false;
        if (k1.super != k2.super)
                return false;

        if (k1.n != k2.n)
                return false;
        if (k1.m != k2.m)
                return false;

        return k1.special == k2.special && utf8_eq(k1.u, k2.u);
}

struct key scan_cs(uint8_t buf[])
{
        bool found_n = false, found_m = false;
        struct key k = {0};

        k.n = 0;
        k.m = 0;

        while (*buf && '0' <= *buf && *buf <= '9') {
                k.n = k.n * 10 + (*buf - '0');
                found_n = true;
                ++buf;
        }
        if (!found_n)
                k.n = 1;

        if (*buf == ';') {
                ++buf;
                while (*buf && '0' <= *buf && *buf <= '9') {
                        k.m = k.m * 10 + (*buf - '0');
                        found_m = true;
                        ++buf;
                }
        }
        if (!found_m)
                k.m = 1;

        assert(*buf);
        switch (*buf) {
        case '~':
                switch (k.n) {
                case 3:
                        k.n = 1;
                        k.special = DEL;
                        break;
                case 5:
                        k.n = 1;
                        k.special = PGUP;
                        break;
                case 6:
                        k.n = 1;
                        k.special = PGDN;
                        break;
                case 11:
                        k.special = F1;
                        break;
                case 12:
                        k.special = F2;
                        break;
                case 13:
                        k.special = F3;
                        break;
                case 14:
                        k.special = F4;
                        break;
                case 15:
                        k.special = F5;
                        break;
                case 17:
                        k.special = F6;
                        break;
                case 18:
                        k.special = F7;
                        break;
                case 19:
                        k.special = F8;
                        break;
                case 20:
                        k.special = F9;
                        break;
                case 21:
                        k.special = F10;
                        break;
                case 23:
                        k.special = F11;
                        break;
                case 24:
                        k.special = F12;
                        break;
                default:
                        assert(0);
                }
                break;
        case 0xd:
                k.meta = 1;
                k.special = CR;
                return k;
        case 'A':
                k.special = CUU;
                break;
        case 'B':
                k.special = CUD;
                break;
        case 'C':
                k.special = CUF;
                break;
        case 'D':
                k.special = CUB;
                break;
        case 'E':
                k.special = CNL;
                break;
        case 'F':
                k.special = CPL;
                break;
        case 'G':
                k.special = CHA;
                break;
        case 'H':
                k.special = CUP;
                break;
        case 'J':
                k.special = ED;
                break;
        case 'K':
                k.special = EL;
                break;
        case 'R':
                k.special = CPR;
                break;
        case 'Z':
                k.shift = 1;
                k.special = HT;
                return k;
        default:
                assert(0);
        }

        k.shift = !!((k.m - 1) & 0x1);
        k.meta = !!((k.m - 1) & 0x2);
        k.ctrl = !!((k.m - 1) & 0x4);
        k.super = !!((k.m - 1) & 0x8);
        k.n = !!k.n;
        k.m = !!k.m;

        return k;
}

struct key scan_escape(uint8_t buf[])
{
        struct key k = {0};

        if (!buf[0]) {
                k.special = ESC;
                return k;
        }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
        switch (buf[0]) {
        case 0x00 ... 0x19: // TODO: seems to generate C-M-[a-zA-Z]. Why?
                k.ctrl = 1;
                k.meta = 1;
                k.u.c[0] = 'a' + (buf[0] - 1);
                return k;
        case 0x5b:
                if (buf[1])
                        return scan_cs(buf + 1);
                k.meta = 1;
                k.u.c[0] = buf[0];
                return k;
        case 0x20 ... 0x5a:
        case 0x5c ... 0x7e:
                if (!buf[1]) {
                        k.meta = 1;
                        k.u.c[0] = buf[0];
                        return k;
                }
                assert(0);
        case 0x7f: /* Non-standard? */
                k.meta = 1;
                k.special = BS;
                return k;
        default:
                assert(0);
        }
#pragma GCC diagnostic pop
}

struct key read_key()
{
        struct key k = {0};
        uint8_t buf[16] = {0};

        ssize_t nread = read(STDIN_FILENO, buf, sizeof(buf));
        assert(nread > 0); // TODO: Exit gracefully.
        buf[min(nread, 15)] = 0;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
        switch (buf[0]) {
        case 0x00:
                k.ctrl = 1;
                k.u.c[0] = ' ';
                return k;
        case 0x07:
                k.special = BEL;
                return k;
        case 0x08:
                k.special = BS;
                return k;
        case 0x09:
                k.special = HT;
                return k;
        case 0x0a:
                k.special = LF;
                return k;
        case 0x0c:
                k.special = FF;
                return k;
        case 0x0d:
                k.special = CR;
                return k;
        case 0x1 ... 0x6:
        case 0xb:
        case 0xe ... 0x1a: /* Missing in this range are handled above. */
                k.ctrl = 1;
                k.u.c[0] = 0x60 + buf[0];
                return k;
        case 0x1b:
                return scan_escape(buf + 1);
        case 0x20 ... 0x7e:
                k.u.c[0] = buf[0];
                return k;
        case 0x7f:
                k.ctrl = 1;
                k.special = BS;
                return k;
        default:
                utf8_char_copy(k.u.c, buf);
                return k;
        }
#pragma GCC diagnostic pop
}

const char *process_modifiers(const char *s, struct key *k)
{
        if (s[0] && s[0] == 'C' && s[1] && s[1] == '-') {
                k->ctrl = 1;
                return process_modifiers(s + 2, k);
        }

        if (s[0] && s[0] == 'M' && s[1] && s[1] == '-') {
                k->meta = 1;
                return process_modifiers(s + 2, k);
        }

        if (s[0] && s[0] == 's' && s[1] && s[1] == '-') {
                k->super = 1;
                return process_modifiers(s + 2, k);
        }

        if (s[0] && s[0] == 'S' && s[1] && s[1] == '-') {
                k->shift = 1;
                return process_modifiers(s + 2, k);
        }

        return s;
}

bool process_special(const char *s, struct key *k)
{
        char buf[32] = {0};

        if (*s++ != '<')
                return false;

        size_t i = 0;
        while (*s && *s != '>')
                buf[i++] = *s++;
        buf[i] = 0;

        if (!buf[0])
                return false;

        if (!strcmp(buf, "up")) {
                k->special = CUU;
                k->n = 1;
                k->m = 1;
                return true;
        }

        if (!strcmp(buf, "down")) {
                k->special = CUD;
                k->n = 1;
                k->m = 1;
                return true;
        }

        if (!strcmp(buf, "left")) {
                k->special = CUB;
                k->n = 1;
                k->m = 1;
                return true;
        }

        if (!strcmp(buf, "right")) {
                k->special = CUF;
                k->n = 1;
                k->m = 1;
                return true;
        }

        if (!strcmp(buf, "return")) {
                k->special = LF;
                k->n = 1;
                k->m = 1;
                return true;
        }

        if (!strcmp(buf, "home")) {
                k->special = CUP;
                k->n = 1;
                k->m = 1;
                return true;
        }

        if (!strcmp(buf, "end")) {
                k->special = CPL;
                k->n = 1;
                k->m = 1;
                return true;
        }

        if (!strcmp(buf, "tab")) {
                k->special = HT;
                return true;
        }

        if (!strcmp(buf, "backspace")) {
                k->special = BS;
                return true;
        }

        if (!strcmp(buf, "delete")) {
                k->special = DEL;
                k->n = 1;
                k->m = 1;
                return true;
        }

        if (!strcmp(buf, "prior")) {
                k->special = PGUP;
                k->n = 1;
                k->m = 1;
                return true;
        }

        if (!strcmp(buf, "next")) {
                k->special = PGDN;
                k->n = 1;
                k->m = 1;
                return true;
        }

        if (!strcmp(buf, "cr")) {
                k->special = CR;
                return true;
        }

        if (!strcmp(buf, "space")) {
                k->u.c[0] = ' ';
                return true;
        }

        return false;
}

struct key kbd(const char *s)
{
        struct key k = {0};

        s = process_modifiers(s, &k);

        if (process_special(s, &k))
                return k;

        if (k.ctrl && !k.meta && !k.super && !k.shift)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
                switch (*s) {
                case 'g':
                        k.ctrl = 0;
                        k.special = BEL;
                        return k;
                default:
                        break;
                }

        switch (*s) {
        case 0x20 ... 0x7e:
                k.u.c[0] = *s;
                return k;
        default:
                assert(0);
        }
#pragma GCC diagnostic pop
}

bool is_digit(struct key k)
{
        return key_eq(k, kbd("0")) || key_eq(k, kbd("1")) || key_eq(k, kbd("2")) ||
               key_eq(k, kbd("3")) || key_eq(k, kbd("4")) || key_eq(k, kbd("5")) ||
               key_eq(k, kbd("6")) || key_eq(k, kbd("7")) || key_eq(k, kbd("8")) ||
               key_eq(k, kbd("9"));
}

void emit_csi(char c, int n, int m)
{
        char buf[32];

        if (n >= 0 && m >= 0)
                snprintf(buf, 32, "\x1b[%d;%d%c", n, m, c);
        else if (n >= 0)
                snprintf(buf, 32, "\x1b[%d%c", n, c);
        else if (m >= 0)
                snprintf(buf, 32, "\x1b[;%d%c", m, c);
        else
                snprintf(buf, 32, "\x1b[%c", c);

        write(STDOUT_FILENO, buf, strlen(buf));
}

void emit_private(char c, int n)
{
        char buf[32];

        snprintf(buf, 32, "\x1b[?%d%c", n, c);

        write(STDOUT_FILENO, buf, strlen(buf));
}

void hide_cursor()
{
        emit_private('l', 25);
}

void show_cursor()
{
        emit_private('h', 25);
}

struct position {
        size_t y, x;
};

void goto_(struct position pos)
{
        emit_csi('H', pos.y, pos.x);
}

void emit_cr()
{
        printf("\r");
}

void emit_el()
{
        printf("\x1b[K");
}

void emit_lf()
{
        printf("\n");
}

void emit_cuu(int n)
{
        emit_csi('A', n, -1);
}

void emit_cud(int n)
{
        emit_csi('B', n, -1);
}

void err_exit(const char *message)
{
        if (errno)
                perror(message);
        else
                fprintf(stderr, "%s", message);

        exit(1);
}

struct termios old_termios;

void terminal_reset()
{
        tcsetattr(STDIN_FILENO, TCSADRAIN, &old_termios);
}

void terminal_setup()
{
        struct termios new_termios;

        if (tcgetattr(STDIN_FILENO, &old_termios) == -1)
                err_exit("terminal_setup: tcgetattr() failed");

        new_termios = old_termios;
        cfmakeraw(&new_termios);

        if (tcsetattr(STDIN_FILENO, TCSADRAIN, &new_termios) == -1)
                err_exit("terminal_setup: tcsetattr() failed");

        atexit(terminal_reset);
}

struct {
        uint8_t b[SCREENBUF_SIZE];
        size_t last;
} screenbuf;

void screenbuf_init()
{
        screenbuf.last = 0;
}

void just_cstring(const char *s)
{
        while (*s)
                screenbuf.b[screenbuf.last++] = *(s++);
}

void just_utf8(struct utf8 u)
{
        utf8_char_copy(screenbuf.b + screenbuf.last, u.c);
        screenbuf.last += utf8_count(u.c);
}

void highlight_on()
{
        just_cstring("\x1b[7m");
}

void highlight_off()
{
        just_cstring("\x1b[m");
}

void csi(char c, int n, int m)
{
        char buf[32];

        if (n >= 0 && m >= 0)
                snprintf(buf, 32, "\x1b[%4d;%4d%c", n, m, c);
        else if (n >= 0)
                snprintf(buf, 32, "\x1b[%4d%c", n, c);
        else if (m >= 0)
                snprintf(buf, 32, "\x1b[;%4d%c", m, c);
        else
                snprintf(buf, 32, "\x1b[%c", c);

        just_cstring(buf);
}

void el()
{
        csi('K', -1, -1);
}

struct position cpr()
{
        struct position p;
        char buf[32];

        emit_csi('n', 6, -1);

        size_t n = read(STDIN_FILENO, buf, sizeof(buf));
        assert(n > 0);
        buf[n] = 0;

        printf("%s", buf);
        sscanf(buf, "\x1b[%zu;%zuR", &p.y, &p.x);

        return p;
}

void save_cursor()
{
        emit_csi('s', -1, -1);
}

void restore_cursor()
{
        emit_csi('u', -1, -1);
}

void cr()
{
        just_cstring("\r");
}

void lf()
{
        just_cstring("\n");
}

struct {
        size_t nlines;
        size_t ncols;
        struct {
                struct {
                        enum { FIRST, LAST, OFFSET } k;
                        size_t offset;
                } position;
        } options;
        struct position screen_begin;
        struct position echo_begin;
        bool is_prefix;
        int prefix_arg;

        size_t tabstop;
        enum { UNIX, DOS } filetype;
        bool ensure_trailing_newline;
        char *filename;
        char *dirname;
        char *basename;
        mode_t filemode;
        struct timespec mtime;
        struct tedchar buffer[BUFSIZE];
        struct tedchar *gap_start;
        struct tedchar *gap_end;
        struct tedchar *tl;
        size_t cursor_row;
        size_t cursor_col;
        size_t goal_col;
        bool force_goal_col;
        struct {
                size_t m[MARK_RING_SIZE];
                size_t len;
                size_t first;
                size_t last;
                size_t current;
                bool is_active;
        } marks;
        struct {
                size_t results[SEARCH_SIZE];
                size_t last;
                size_t current;
        } search;
        bool is_read_only;
        bool is_dirty;
        struct key last_key;
        bool preserve_echo;
        struct tedchar kill_buffer[BUFSIZE];
        size_t kill_size;
} ed;

void screenbuf_draw()
{
        save_cursor();

        goto_(ed.screen_begin);
        write(STDOUT_FILENO, screenbuf.b, screenbuf.last);

        restore_cursor();
}

/*
  Only UTF8_ASCII allowed in echo messages.
*/
void sanitize(char buf[])
{
        size_t i = 0;
        while (buf[i]) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
                switch (buf[i]) {
                case 0x20 ... 0x7e:
                        ++i;
                        break;
                default:
                        buf[i] = '?';
                        ++i;
                }
#pragma GCC diagnostic pop
        }
        buf[ed.ncols] = 0;
}

void echo_clear()
{
        save_cursor();

        goto_(ed.echo_begin);
        write(STDOUT_FILENO, "\x1b[K", strlen("\x1b[K"));

        restore_cursor();
}

void echo_error(const char *message, ...)
{
        char buf[512];
        va_list ap;

        va_start(ap, message);

        size_t n = snprintf(buf, sizeof(buf), ERROR_PRE);
        n += vsnprintf(buf + n, sizeof(buf) - n, message, ap);
        n += snprintf(buf + n, sizeof(buf) - n, "\x1b[m\x1b[K");

        save_cursor();

        goto_(ed.echo_begin);
        write(STDOUT_FILENO, buf, strlen(buf));

        restore_cursor();

        ed.preserve_echo = true;
}

void echo_info(const char *message, ...)
{
        char buf[512];
        va_list ap;

        va_start(ap, message);

        size_t n = snprintf(buf, sizeof(buf), INFO_PRE);
        n += vsnprintf(buf + n, sizeof(buf) - n, message, ap);
        n += snprintf(buf + n, sizeof(buf) - n, "\x1b[m\x1b[K");

        save_cursor();

        goto_(ed.echo_begin);
        write(STDOUT_FILENO, buf, strlen(buf));

        restore_cursor();
}

#define echo_info_preserve(message, ...)           \
        do {                                       \
                echo_info(message, ##__VA_ARGS__); \
                ed.preserve_echo = true;           \
        } while (0)

void emit_clear_screen()
{
        goto_(ed.screen_begin);
        emit_csi('J', -1, -1);
}

void reserve_screen()
{
        for (size_t i = 0; i < ed.nlines; ++i) {
                emit_cr();
                emit_el();
                emit_lf();
        }

        emit_el();

        emit_cuu(ed.nlines);

        ed.screen_begin = cpr();

        emit_cud(ed.nlines);
        ed.echo_begin = cpr();

        goto_(ed.screen_begin);
}

void move_point(struct tedchar *p)
{
        assert(p);

        if (p == ed.gap_end)
                return;

        if (p < ed.gap_start) {
                ptrdiff_t n = ed.gap_start - p;
                struct tedchar *d = ed.gap_end;
                memmove(d - n, p, n * sizeof(struct tedchar));
                ed.gap_start = p;
                ed.gap_end = d - n;
        } else if (p > ed.gap_end) {
                struct tedchar *d = ed.gap_start;
                struct tedchar *s = ed.gap_end;
                memmove(d, s, (p - s) * sizeof(struct tedchar));
                ed.gap_start += p - s;
                ed.gap_end += p - s;
        }
}

size_t tedchar_from_bytes(struct tedchar dest[], size_t n, const uint8_t src[], size_t m)
{
        size_t i = 0;
        size_t j = 0;

        while (j < m) {
                if (ed.filetype == DOS && src[j] == '\r') {
                        if (j + 1 < m && src[j + 1] == '\n') {
                                dest[i++] = tedchar_newline();
                                j += 2;
                        } else {
                                err_exit("<cr> not followed by <lf> in file.\n");
                        }
                } else if (ed.filetype == UNIX && src[j] == '\n') {
                        dest[i++] = tedchar_newline();
                        ++j;
                } else {
                        size_t k = utf8_count(&src[j]);
                        if (j + k - 1 >= m) {
                                err_exit("Invalid utf8 in file.\n");
                        }

                        if (k == 1)
                                if (src[j] != '\t' && (src[j] < 0x20 || src[j] > 0x7e))
                                        err_exit("Invalid ASCII in file.\n");
                        assert(i < n);
                        for (size_t x = 0; x < k; ++x)
                                dest[i].u.c[x] = src[j++];
                        dest[i].kind = UTF8;
                        ++i;
                }
        }

        return i;
}

void disable_mark()
{
        ed.marks.is_active = false;
}

void loadf(const char *filename)
{
        int fd;

        char *rp = realpath(filename, NULL);
        if (!rp) {
                rp = strdup(filename);
        }
        if (!rp) {
                perror("loadf: strdup() failed");
                return;
        }

        char *d1 = strdup(rp);
        char *b1 = strdup(rp);
        if (!d1 || !b1) {
                free(d1);
                free(b1);
                perror("loadf: strdup() failed");
                return;
        }

        char *d = strdup(dirname(d1));
        char *b = strdup(basename(b1));
        if (!d || !b) {
                free(d);
                free(b);
                perror("loadf: strdup() failed");
                return;
        }

        free(d1);
        free(b1);

        struct stat st;

        if (stat(d, &st)) {
                perror("loadf: stat() failed");
                goto err3;
        }

        if (!S_ISDIR(st.st_mode)) {
                fprintf(stderr, "loadf: '%s': not a directory.\n", d);
                goto err3;
        }

        if (access(d, R_OK) != 0) {
                fprintf(stderr, "loadf: Cannot read directory '%s'.\n", d);
                goto err3;
        }

        if ((fd = open(filename, O_RDONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0) {
                perror("loadf: Failed to open file");
                goto err3;
        }

        if (fstat(fd, &st) < 0) {
                perror("loadf: fstat() failed");
                goto err2;
        }

        ssize_t n = 0;

        uint8_t *buf = malloc(st.st_size);
        if (!buf) {
                perror("loadf: malloc() failed");
                goto err2;
        }

        while (n < st.st_size) {
                ssize_t r = read(fd, buf + n, st.st_size - n);

                if (r < 0) {
                        perror("loadf: read() failed");
                        goto err1;
                }

                if (r == 0)
                        break;

                n += r;
        }

        close(fd);

        n = tedchar_from_bytes(ed.buffer, BUFSIZE, buf, st.st_size);

        free(buf);

        ed.filename = rp;
        ed.dirname = d;
        ed.basename = b;
        ed.filemode = st.st_mode;
        ed.mtime = st.st_mtim;

        ed.ensure_trailing_newline = true;

        ed.gap_start = ed.buffer + n;
        ed.gap_end = ed.buffer + BUFSIZE;
        ed.cursor_row = 0;
        ed.cursor_col = 0;
        ed.goal_col = 0;
        ed.tl = NULL;

        ed.marks.len = 0;
        ed.marks.first = 0;
        ed.marks.last = 0;
        ed.marks.current = 0;
        ed.marks.is_active = false;

        ed.search.last = 0;
        ed.search.current = 0;

        ed.is_read_only = false;
        ed.is_dirty = false;

        ed.preserve_echo = false;

        ed.kill_size = 0;

        if (n) {
                move_point(ed.buffer);
                ed.tl = ed.gap_end;
        }

        return;

err1:
        free(buf);
err2:
        close(fd);
err3:
        exit(1);
}

struct tedchar *advance(struct tedchar *p)
{
        if (p >= ed.gap_end) {
                if (p + 1 < ed.buffer + BUFSIZE)
                        return p + 1;
                else
                        return NULL;
        }

        if (p + 1 < ed.gap_start)
                return p + 1;

        if (ed.gap_end < ed.buffer + BUFSIZE)
                return ed.gap_end;

        return NULL;
}

struct tedchar *retreat(struct tedchar *p)
{
        if (p == ed.buffer)
                return NULL;

        if (p == ed.gap_end) {
                if (ed.gap_start == ed.buffer)
                        return NULL;
                else
                        return ed.gap_start - 1;
        }

        return p - 1;
}

size_t next_col(struct tedchar t, size_t col)
{
        assert(col < ed.ncols);

        if (is_newline(t)) {
                return 0;
        } else if (is_tab(t)) {
                size_t new_col = col + ed.tabstop - col % ed.tabstop;
                if (new_col >= ed.ncols)
                        return 0;
                return new_col;
        } else {
                if (col + 1 >= ed.ncols)
                        return 0;
                return col + 1;
        }
}

bool is_point_at_beginning_of_buffer()
{
        return ed.gap_start == ed.buffer;
}

bool is_point_at_end_of_buffer()
{
        return ed.gap_end == ed.buffer + BUFSIZE;
}

bool is_buffer_empty()
{
        return (ed.gap_end - ed.gap_start) == BUFSIZE;
}

size_t buffer_size()
{
        return (ed.gap_start - ed.buffer) + (ed.buffer + BUFSIZE - ed.gap_end);
}

struct tedchar *first_char()
{
        if (is_buffer_empty())
                return NULL;

        if (ed.gap_start > ed.buffer)
                return ed.buffer;

        return ed.gap_end;
}

struct tedchar *char_at_point()
{
        if (is_buffer_empty() || is_point_at_end_of_buffer())
                return NULL;

        return ed.gap_end;
}

struct tedchar *char_at_index(size_t i)
{
        if (i >= buffer_size())
                return NULL;

        size_t n = ed.gap_start - ed.buffer;

        if (i < n)
                return &ed.buffer[i];
        else
                return &ed.gap_end[i - n];
}

size_t index_of(struct tedchar *t)
{
        if (t < ed.gap_start)
                return t - ed.buffer;
        else
                return (ed.gap_start - ed.buffer) + (t - ed.gap_end);
}

size_t col_of(struct tedchar *p)
{
        assert(p);

        struct tedchar *q = retreat(p);

        if (!q)
                return 0;

        struct tedchar *qq = q;
        while (qq && !is_newline(*qq)) {
                q = qq;
                qq = retreat(qq);
        }

        size_t col = 0;
        while (q != p) {
                col = next_col(*q, col);
                q = advance(q);
        }

        return col;
}

struct tedchar *first_of_visual_line(struct tedchar *p)
{
        assert(p);

        struct tedchar *q = retreat(p);

        if (!q)
                return p;

        struct tedchar *qq = q;
        while (qq && !is_newline(*qq)) {
                q = qq;
                qq = retreat(qq);
        }

        size_t col = 0;
        struct tedchar *r = q;
        while (q != p) {
                col = next_col(*q, col);
                q = advance(q);
                if (col == 0)
                        r = q;
        }

        return r;
}

size_t where()
{
        if (is_point_at_end_of_buffer())
                return buffer_size();
        return index_of(char_at_point());
}

void point_mark_low_high(size_t *low, size_t *high)
{
        size_t p = where();
        size_t m = ed.marks.m[ed.marks.current];

        *low = p <= m ? p : m;
        *high = p <= m ? m : p;
}

void refresh()
{
        hide_cursor();

        screenbuf_init();

        size_t low = 0, high = 0;

        if (ed.marks.is_active) {
                point_mark_low_high(&low, &high);
        }

        bool highlight_active = false;

        struct tedchar *current = ed.tl;

        for (size_t lines = 0; lines < ed.nlines; ++lines) {
                size_t col = 0;
                bool line = false;
                bool newline = false;

                while (current) {
                        if (ed.marks.is_active && !highlight_active && index_of(current) >= low &&
                            index_of(current) < high) {
                                highlight_on();
                                highlight_active = true;
                        }

                        if (ed.marks.is_active && highlight_active && index_of(current) == high) {
                                highlight_off();
                                highlight_active = false;
                        }

                        line = true;

                        assert(col <= ed.ncols);

                        if (col == ed.ncols) {
                                if (highlight_active)
                                        highlight_off();
                                just_cstring(CONTINUATION_LINE_STR);
                                el();
                                cr();
                                lf();
                                if (highlight_active)
                                        highlight_on();
                                break;
                        } else if (is_newline(*current)) {
                                newline = true;
                                just_cstring(" ");
                                el();
                                cr();
                                lf();
                                current = advance(current);
                                break;
                        } else if (is_tab(*current)) {
                                size_t new_col = next_col(*current, col);
                                current = advance(current);
                                if (new_col == 0) {
                                        while (col < ed.ncols) {
                                                just_cstring(" ");
                                                ++col;
                                        }
                                        just_cstring(CONTINUATION_LINE_STR);
                                        el();
                                        cr();
                                        lf();
                                        break;
                                } else {
                                        while (col < new_col) {
                                                just_cstring(" ");
                                                ++col;
                                        }
                                }
                        } else {
                                assert(col < ed.ncols);
                                just_utf8(current->u); // Assumes width-1.

                                size_t new_col = next_col(*current, col);
                                current = advance(current);
                                if (new_col == 0) {
                                        if (highlight_active)
                                                highlight_off();
                                        just_cstring(CONTINUATION_LINE_STR);
                                        el();
                                        cr();
                                        lf();
                                        if (highlight_active)
                                                highlight_on();
                                        break;
                                }
                                col = new_col;
                        }
                }

                if (!line) {
                        if (highlight_active)
                                highlight_off();
                        just_cstring(EMPTY_LINE_STR);
                        el();
                        cr();
                        lf();
                        if (highlight_active)
                                highlight_on();
                } else if (!newline && !current) {
                        el();
                        cr();
                        lf();
                }
        }

        screenbuf_draw();

        goto_((struct position){
                .y = ed.screen_begin.y + ed.cursor_row,
                .x = ed.screen_begin.x + ed.cursor_col,
        });
        show_cursor();
}

void previous_row();
void next_row();
void beginning_of_row();
void end_of_row();
void beginning_of_buffer();

void scroll_down()
{
        if (is_buffer_empty())
                return;

        size_t repeat = ed.is_prefix ? ed.prefix_arg : 1;

        ed.is_prefix = false;

        while (repeat--) {
                if (ed.cursor_row == ed.nlines - 1) {
                        previous_row();
                }

                struct tedchar *p = ed.tl;
                if (!p)
                        return;

                struct tedchar *q = retreat(p);
                if (!q)
                        return;

                ed.tl = first_of_visual_line(q);
                ++ed.cursor_row;
        }
}

void scroll_up()
{
        if (is_buffer_empty())
                return;

        size_t repeat = ed.is_prefix ? ed.prefix_arg : 1;

        ed.is_prefix = false;

        while (repeat--) {
                if (ed.cursor_row == 0) {
                        next_row();
                }

                struct tedchar *p = ed.tl;
                size_t n = 0;

                if (!p)
                        return;

                while (1) {
                        n = next_col(*p, n);
                        p = advance(p);
                        if (!p)
                                return;
                        if (n == 0)
                                break;
                }

                ed.tl = p;
                --ed.cursor_row;
        }
}

void forward_char()
{
        if (is_buffer_empty())
                return;

        size_t repeat = ed.is_prefix ? ed.prefix_arg : 1;

        ed.is_prefix = false;

        while (repeat--) {
                if (is_point_at_end_of_buffer())
                        return;

                struct tedchar *c = char_at_point();

                if (ed.cursor_row == ed.nlines - 1 && next_col(*c, ed.cursor_col) == 0) {
                        scroll_up();
                        c = char_at_point();
                }

                if (ed.cursor_row == 0 && ed.cursor_col == 0) {
                        ed.tl = ed.gap_start;
                }

                *ed.gap_start = *c;
                ++ed.gap_start;
                ++ed.gap_end;
                if (next_col(*c, ed.cursor_col) == 0) {
                        ++ed.cursor_row;
                }
                ed.cursor_col = next_col(*c, ed.cursor_col);

                if (!ed.force_goal_col)
                        ed.goal_col = ed.cursor_col;
        }
}

void backward_char()
{
        if (is_buffer_empty())
                return;

        size_t repeat = ed.is_prefix ? ed.prefix_arg : 1;

        ed.is_prefix = false;

        while (repeat--) {
                if (is_point_at_beginning_of_buffer())
                        return;

                if (ed.cursor_row == 0 && ed.cursor_col == 0) {
                        scroll_down();
                }

                if (ed.gap_start > ed.buffer) {
                        --ed.gap_end;
                        --ed.gap_start;
                        *ed.gap_end = *ed.gap_start;
                        if (is_newline(*ed.gap_end) || ed.cursor_col == 0)
                                --ed.cursor_row;
                        ed.cursor_col = col_of(ed.gap_end);
                }

                if (ed.cursor_row == 0 && ed.cursor_col == 0) {
                        ed.tl = char_at_point();
                }

                if (!ed.force_goal_col)
                        ed.goal_col = ed.cursor_col;
        }
}

void forward_word()
{
        if (is_buffer_empty())
                return;

        size_t repeat = ed.is_prefix ? ed.prefix_arg : 1;

        ed.is_prefix = false;

        while (repeat--) {
                if (is_point_at_end_of_buffer())
                        return;

                while (!is_point_at_end_of_buffer() && is_whitespace(*char_at_point()))
                        forward_char();

                while (!is_point_at_end_of_buffer() && !is_whitespace(*char_at_point()))
                        forward_char();
        }
}

bool is_point_at_beginning_of_word()
{
        if (is_buffer_empty())
                return false;

        if (is_point_at_end_of_buffer())
                return false;

        struct tedchar *p = char_at_point();

        if (is_point_at_beginning_of_buffer())
                return !is_whitespace(*p);

        struct tedchar *q = retreat(p);

        return !is_whitespace(*p) && is_whitespace(*q);
}

void backward_word()
{
        if (is_buffer_empty())
                return;

        size_t repeat = ed.is_prefix ? ed.prefix_arg : 1;

        ed.is_prefix = false;

        while (repeat--) {
                if (is_point_at_beginning_of_word() || is_point_at_end_of_buffer())
                        backward_char();

                while (is_whitespace(*char_at_point())) {
                        backward_char();
                        if (is_point_at_beginning_of_buffer())
                                return;
                }

                while (!is_whitespace(*char_at_point())) {
                        backward_char();
                        if (is_point_at_beginning_of_buffer())
                                return;
                }

                forward_char();
        }
}

void forward_paragraph()
{
        if (is_buffer_empty())
                return;

        size_t repeat = ed.is_prefix ? ed.prefix_arg : 1;

        ed.is_prefix = false;

        while (repeat--) {
                if (is_point_at_end_of_buffer())
                        return;

                while (!is_point_at_end_of_buffer() && is_whitespace(*char_at_point()))
                        forward_char();

                size_t newline_run = 0;
                while (!is_point_at_end_of_buffer()) {
                        if (is_newline(*char_at_point())) {
                                ++newline_run;
                                if (newline_run == 2)
                                        break;
                        } else {
                                newline_run = 0;
                        }

                        forward_char();
                }
        }
}

void backward_paragraph()
{
        if (is_buffer_empty())
                return;

        size_t repeat = ed.is_prefix ? ed.prefix_arg : 1;

        ed.is_prefix = false;

        while (repeat--) {
                if (is_point_at_beginning_of_buffer())
                        return;

                backward_char();

                while (!is_point_at_beginning_of_buffer() && is_whitespace(*char_at_point()))
                        backward_char();

                size_t newline_run = 0;
                while (!is_point_at_beginning_of_buffer()) {
                        if (is_newline(*char_at_point())) {
                                ++newline_run;
                                if (newline_run == 2) {
                                        while (!is_point_at_end_of_buffer() &&
                                               is_whitespace(*char_at_point()))
                                                forward_char();
                                        break;
                                }
                        } else {
                                newline_run = 0;
                        }

                        backward_char();
                }
        }
}

void next_row()
{
        size_t repeat = ed.is_prefix ? ed.prefix_arg : 1;

        ed.is_prefix = false;

        while (repeat--) {
                if (ed.cursor_row == ed.nlines - 1) {
                        scroll_up();
                }

                size_t save_goal = ed.goal_col;

                end_of_row();
                forward_char();
                beginning_of_row();

                while (1) {
                        struct tedchar *p = char_at_point();
                        if (ed.cursor_col >= save_goal || !p || is_newline(*p)) {
                                ed.goal_col = save_goal;
                                break;
                        }
                        forward_char();
                }
        }
}

void previous_row()
{
        size_t repeat = ed.is_prefix ? ed.prefix_arg : 1;

        ed.is_prefix = false;

        while (repeat--) {
                if (ed.cursor_row == 0) {
                        scroll_down();
                }

                size_t save_goal = ed.goal_col;

                beginning_of_row();
                backward_char();
                beginning_of_row();

                while (1) {
                        struct tedchar *p = char_at_point();

                        if (ed.cursor_col >= save_goal || !p || is_newline(*p)) {
                                ed.goal_col = save_goal;
                                break;
                        }

                        forward_char();
                }
        }
}

void beginning_of_row()
{
        while (ed.cursor_col > 0)
                backward_char();

        if (!ed.force_goal_col)
                ed.goal_col = 0;
}

void end_of_row()
{
        struct tedchar *p = char_at_point();
        while (p) {
                size_t n = next_col(*p, ed.cursor_col);
                if (n == 0)
                        break;
                forward_char();
                p = char_at_point();
        }
}

void beginning_of_line()
{
        if (is_buffer_empty())
                return;

        if (char_at_point() && is_newline(*char_at_point()))
                backward_char();

        while (1) {
                if (is_point_at_beginning_of_buffer())
                        return;

                if (char_at_point() && is_newline(*char_at_point())) {
                        forward_char();
                        return;
                }

                backward_char();
        }
}

void end_of_line()
{
        if (is_buffer_empty())
                return;

        while (!is_point_at_end_of_buffer() && !is_newline(*char_at_point()))
                forward_char();
}

void goto_line()
{
        size_t line_no = ed.prefix_arg;

        if (!ed.is_prefix || ed.prefix_arg < 1)
                line_no = 1;

        ed.is_prefix = false;

        beginning_of_buffer();
        while (--line_no) {
                end_of_line();
                forward_char();
        }
}

void move_to(size_t n)
{
        beginning_of_buffer();
        while (n--)
                forward_char();
}

void goto_percent()
{
        size_t percent = ed.prefix_arg;

        if (!ed.is_prefix || ed.prefix_arg < 0)
                percent = 0;
        else if (ed.prefix_arg > 100)
                percent = 100;

        move_to((buffer_size() * percent) / 100);
}

void beginning_of_buffer()
{
        while (ed.gap_start > ed.buffer)
                backward_char();
}

void end_of_buffer()
{
        while (char_at_point())
                forward_char();
}

void page_down()
{
        size_t repeat = ed.is_prefix ? ed.prefix_arg : 1;

        ed.is_prefix = false;

        while (repeat--) {
                for (size_t i = 0; i < (ed.nlines + 2) / 2; ++i) {
                        scroll_up();
                        next_row();
                }
        }
}

void page_up()
{
        size_t repeat = ed.is_prefix ? ed.prefix_arg : 1;

        ed.is_prefix = false;

        while (repeat--) {
                for (size_t i = 0; i < (ed.nlines + 2) / 2; ++i) {
                        scroll_down();
                        previous_row();
                }
        }
}

void set_goal_column()
{
        if (ed.is_prefix) {
                ed.force_goal_col = false;
        } else {
                ed.force_goal_col = true;
                ed.goal_col = ed.cursor_col;
        }
}

bool is_textchar(struct key k)
{
        if (key_eq(k, kbd("<cr>")) || key_eq(k, kbd("<tab>")))
                return true;

        if (k.special)
                return false;

        if (k.ctrl || k.meta || k.super || k.shift)
                return false;

        if (utf8_count(k.u.c) == 1)
                return 0x20 <= k.u.c[0] && k.u.c[0] <= 0x7e;

        return true;
}

void do_insert_char(struct tedchar t)
{
        ed.is_dirty = true;

        if (ed.gap_start < ed.gap_end) {
                *ed.gap_start = t;
                if (ed.cursor_row == 0 && ed.cursor_col == 0)
                        ed.tl = ed.gap_start;
                ++ed.gap_start;
                size_t new_col = next_col(t, ed.cursor_col);
                if (new_col == 0) {
                        if (ed.cursor_row == ed.nlines - 1)
                                scroll_up();
                        ++ed.cursor_row;
                }
                ed.cursor_col = new_col;
                if (!ed.force_goal_col)
                        ed.goal_col = ed.cursor_col;
        } else {
                assert(0); // TODO: Re-allocate or save and quit.
        }
}

void insert_char()
{
        struct key k = ed.last_key;

        assert(is_textchar(k));

        guard(!ed.is_read_only);

        struct tedchar t;

        if (key_eq(k, kbd("<cr>")))
                t = tedchar_newline();
        else if (key_eq(k, kbd("<tab>")))
                t = tedchar_utf8(utf8_ascii('\t'));
        else
                t = tedchar_utf8(k.u);

        size_t repeat = ed.is_prefix ? ed.prefix_arg : 1;

        ed.is_prefix = false;

        while (repeat--) {
                do_insert_char(t);
        }
}

void open_line()
{
        guard(!ed.is_read_only);

        size_t repeat = ed.is_prefix ? ed.prefix_arg : 1;

        ed.is_prefix = false;

        while (repeat--) {
                do_insert_char(tedchar_newline());
                backward_char();
        }
}

void open_next_line()
{
        guard(!ed.is_read_only);

        size_t repeat = ed.is_prefix ? ed.prefix_arg : 1;

        ed.is_prefix = false;

        while (repeat--) {
                end_of_line();
                do_insert_char(tedchar_newline());
        }
}

void open_previous_line()
{
        guard(!ed.is_read_only);

        size_t repeat = ed.is_prefix ? ed.prefix_arg : 1;

        ed.is_prefix = false;

        while (repeat--) {
                beginning_of_line();
                open_line();
        }
}

void delete_char()
{
        guard(!ed.is_read_only);

        size_t repeat = ed.is_prefix ? ed.prefix_arg : 1;

        ed.is_prefix = false;

        while (repeat--) {
                if (is_buffer_empty() || is_point_at_end_of_buffer())
                        return;

                ed.is_dirty = true;

                if (ed.cursor_row == ed.nlines - 1 &&
                    next_col(*char_at_point(), ed.cursor_col) == 0)
                        scroll_up();

                if (ed.tl == ed.gap_end) {
                        ed.tl = advance(ed.gap_end);
                }

                ++ed.gap_end;
        }
}

void delete_region()
{
        guard(!ed.is_read_only);

        size_t low, high;

        point_mark_low_high(&low, &high);

        struct tedchar *p = char_at_index(low);

        if (!p)
                return;

        size_t nchars = high - low;

        if (nchars == 0)
                return;

        move_to(low);

        while (nchars--)
                delete_char();
}

void delete_backward_char()
{
        guard(!ed.is_read_only);

        if (ed.marks.is_active) {
                delete_region();
                disable_mark();
                return;
        }

        size_t repeat = ed.is_prefix ? ed.prefix_arg : 1;

        ed.is_prefix = false;

        while (repeat--) {
                if (is_buffer_empty() || is_point_at_beginning_of_buffer())
                        return;

                backward_char();
                delete_char();
        }
}

void delete_forward_char()
{
        guard(!ed.is_read_only);

        if (ed.marks.is_active) {
                delete_region();
                disable_mark();
                return;
        }

        delete_char();
}

static void maybe_insert_trailing_newline()
{
        if (!ed.ensure_trailing_newline || is_buffer_empty())
                return;

        struct tedchar *p = char_at_index(buffer_size() - 1);
        if (is_newline(*p))
                return;

        if (is_point_at_end_of_buffer()) {
                do_insert_char(tedchar_newline());
        } else {
                size_t save = index_of(char_at_point());

                end_of_buffer();
                do_insert_char(tedchar_newline());

                move_to(save);
        }
}

#define for_each_block(buf, sz, i, body)                                 \
        do {                                                             \
                struct tedchar *p = first_char();                        \
                while (p) {                                              \
                        while (p) {                                      \
                                if (is_newline(*p)) {                    \
                                        if (ed.filetype == UNIX) {       \
                                                if (i >= sz)             \
                                                        break;           \
                                                                         \
                                                buf[i++] = '\n';         \
                                        } else if (ed.filetype == DOS) { \
                                                if (i >= sz - 1)         \
                                                        break;           \
                                                                         \
                                                buf[i++] = '\r';         \
                                                buf[i++] = '\n';         \
                                        }                                \
                                } else {                                 \
                                        size_t k = utf8_count(p->u.c);   \
                                                                         \
                                        if (i + k >= sz)                 \
                                                break;                   \
                                                                         \
                                        for (size_t j = 0; j < k; ++j)   \
                                                buf[i++] = p->u.c[j];    \
                                }                                        \
                                p = advance(p);                          \
                        }                                                \
                        body;                                            \
                        i = 0;                                           \
                }                                                        \
        } while (0)

int write_all(int fd, uint8_t *buf, size_t n)
{
        ssize_t r;
        int retries = 10;

        while (n) {
                r = write(fd, buf, n);
                if (r > 0) {
                        n -= r;
                } else if (r < 0) {
                        switch (errno) {
                        case EINTR:
                                if (!retries)
                                        return EINTR;
                                --retries;
                                break;
                        default:
                                return r;
                        }
                } else {
                        if (!retries)
                                return EAGAIN;
                        --retries;
                }
        }

        return 0;
}

int open_save_file(const char *dirname, const char *basename, char *buf, size_t n)
{
        int fd;
        int flags = O_CREAT | O_TRUNC | O_WRONLY | O_EXCL;

        for (int i = 0; i < 100; ++i) {
                snprintf(buf, n, "%s/.%s.%d", dirname, basename, i);
                if (access(buf, F_OK)) {
                        fd = open(buf, flags, ed.filemode);
                        return fd;
                }
        }

        return -1;
}

bool timespec_lt(struct timespec a, struct timespec b)
{
        return a.tv_sec < b.tv_sec || a.tv_nsec < b.tv_nsec;
}

void write_buffer_to_file(int fd)
{
        uint8_t buf[BUFSIZE] = {0};
        size_t i = 0;

        for_each_block(buf, BUFSIZE, i, {
                if (write_all(fd, buf, i))
                        return;
        });
}

void save_buffer()
{
        maybe_insert_trailing_newline();

        char pathbuf[PATH_MAX];

        int fd = open_save_file(ed.dirname, ed.basename, pathbuf, PATH_MAX);
        if (fd < 0) {
                fd = open_save_file(P_tmpdir, ed.basename, pathbuf, PATH_MAX);
                if (fd < 0) {
                        echo_error("Failed to save file.");
                        return;
                }
        }

        write_buffer_to_file(fd);

        if (close(fd)) {
                echo_error("Failed to save file.");
                return;
        }

        struct stat st;
        stat(ed.filename, &st);

        if (timespec_lt(ed.mtime, st.st_mtim)) {
                echo_error("File has been modified. Wrote to \'%s\'", pathbuf);
                return;
        }

        if (rename(pathbuf, ed.filename)) {
                echo_error("\'%s\' rename failed.", pathbuf);
                return;
        }

        unlink(pathbuf);

        echo_info_preserve("Wrote \'%s\'", ed.filename);
        ed.is_dirty = false;
        if (!stat(ed.filename, &st))
                ed.mtime = st.st_mtim;
}

void do_push_mark(size_t w)
{
        ed.marks.m[ed.marks.last] = w;
        ed.marks.current = ed.marks.last;
        ed.marks.last = (ed.marks.last + 1) % MARK_RING_SIZE;

        if (ed.marks.len == MARK_RING_SIZE)
                ++ed.marks.first;
        else
                ++ed.marks.len;
}

void exchange_point_and_mark()
{
        if (is_buffer_empty())
                return;

        if (!ed.marks.len)
                return;

        size_t save = ed.marks.m[ed.marks.current];
        ed.marks.m[ed.marks.current] = where();

        move_to(save);
}

void set_mark()
{
        if (ed.is_prefix) {
                if (!ed.marks.len)
                        return;

                exchange_point_and_mark();

                if (ed.marks.len <= 1)
                        return;

                if (ed.marks.current == ed.marks.first)
                        if (ed.marks.last > 0)
                                ed.marks.current = ed.marks.last - 1;
                        else
                                ed.marks.current = MARK_RING_SIZE - 1;
                else if (ed.marks.current == 0)
                        ed.marks.current = MARK_RING_SIZE - 1;
                else
                        ed.marks.current -= 1;
                return;
        }

        do_push_mark(where());
        ed.marks.is_active = true;
}

void set_mark_forward_word()
{
        set_mark();
        forward_word();
}

void set_mark_backward_word()
{
        set_mark();
        backward_word();
}

void set_mark_forward_paragraph()
{
        set_mark();
        forward_paragraph();
}

void set_mark_backward_paragraph()
{
        set_mark();
        backward_paragraph();
}

void set_mark_next_row()
{
        set_mark();
        next_row();
}

void set_mark_previous_row()
{
        set_mark();
        previous_row();
}

void set_mark_forward_char()
{
        set_mark();
        forward_char();
}

void set_mark_backward_char()
{
        set_mark();
        backward_char();
}

void kill_region_save()
{
        guard(!ed.is_read_only);

        if (!ed.marks.is_active)
                return;

        ed.kill_size = 0;

        size_t low, high;

        point_mark_low_high(&low, &high);

        struct tedchar *t = char_at_index(low);
        struct tedchar *last = char_at_index(high);

        while (t && t != last) {
                ed.kill_buffer[ed.kill_size++] = *t;
                t = advance(t);
        }

        ed.marks.is_active = false;
}

void kill_region()
{
        if (!ed.marks.is_active)
                return;

        kill_region_save();

        size_t low, high;

        point_mark_low_high(&low, &high);

        size_t nchars = high - low;

        move_to(low);
        while (nchars--)
                delete_char();

        ed.marks.is_active = false;
}

void yank()
{
        guard(!ed.is_read_only);

        size_t repeat = ed.is_prefix ? ed.prefix_arg : 1;

        ed.is_prefix = false;

        while (repeat--)
                for (size_t i = 0; i < ed.kill_size; ++i) {
                        do_insert_char(ed.kill_buffer[i]);
                }
}

void show_line_column()
{
        struct tedchar *p = char_at_point();
        struct tedchar *t = char_at_index(0);

        size_t line_no = 1;
        size_t col_no = 1;

        while (t != p) {
                if (is_newline(*t)) {
                        ++line_no;
                        col_no = 1;
                } else {
                        ++col_no;
                }

                t = advance(t);
        }

        echo_info_preserve("L%uC%u", line_no, col_no);
}

void toggle_read_only_mode()
{
        ed.is_read_only = !ed.is_read_only;
        echo_info_preserve("Read-Only mode %s.", ed.is_read_only ? "enabled" : "disabled");
}

void search_previous()
{
        if (!ed.search.last)
                return;

        if (ed.search.current == 0) {
                echo_info_preserve("Wrapped backward search");
                ed.search.current = ed.search.last - 1;
        } else {
                --ed.search.current;
        }

        move_to(ed.search.results[ed.search.current]);
}

void search_next()
{
        if (!ed.search.last)
                return;

        ++ed.search.current;
        if (ed.search.current == ed.search.last) {
                echo_info_preserve("Wrapped search");
                ed.search.current = 0;
        }

        move_to(ed.search.results[ed.search.current]);
}

void search_buffer()
{
        char cmd[CMD_MAX];
        char tmp[] = "/tmp/ted-search-XXXXXX";
        const char *e;

        if (ed.search.last) {
                search_next();
                return;
        }

        int fd = mkstemp(tmp);
        if (fd < 0) {
                echo_error("Failed to start search");
                return;
        }

        write_buffer_to_file(fd);

        if (close(fd)) {
                echo_error("Failed to start search");
                return;
        }

        if ((e = getenv("TED_SEARCH")))
                snprintf(cmd, CMD_MAX, "%s \'%s\' %zd", e, tmp, ed.nlines + 1);
        else
                snprintf(cmd, CMD_MAX,
                         "printf '\\e[s' > /dev/tty; "
                         "read -p 'Query: ' query; "
                         "printf '\\e[u\\e[J' > /dev/tty; "
                         "grep -bo -F \"$query\" \'%s\' | cut -d: -f1 ",
                         tmp);

        emit_clear_screen();
        terminal_reset();

        FILE *sout;
        int r = 1;
        if ((sout = popen(cmd, "r"))) {
                ed.search.last = 0;
                while (!feof(sout))
                        if (fscanf(sout, "%zu\n", &ed.search.results[ed.search.last]) == 1)
                                ++ed.search.last;
                        else
                                break;

                r = pclose(sout);
        }

        unlink(tmp);

        terminal_setup();
        reserve_screen();
        refresh();

        if (r) {
                echo_info_preserve("Search failed");
        } else if (!ed.search.last) {
                echo_info_preserve("No results");
        } else {
                do_push_mark(where());
                move_to(ed.search.results[ed.search.current = 0]);
        }
}

void search_quit()
{
        ed.search.last = 0;
}

void quit()
{
        if (ed.is_dirty) {
                if (ed.is_prefix) {
                        save_buffer();
                        if (!ed.is_dirty)
                                goto exit_success;
                }

                echo_error("Save and quit: C-u C-x C-c. Quit without saving: C-x M-c.");
                return;
        }

exit_success:
        emit_clear_screen();
        free(ed.filename);
        free(ed.dirname);
        free(ed.basename);
        exit(0);
}

void kill_ted()
{
        emit_clear_screen();
        exit(1);
}

void cancel()
{
        ed.marks.is_active = false;
        echo_clear();
}

void suspend()
{
        emit_clear_screen();
        terminal_reset();
        raise(SIGTSTP);
        terminal_setup();
        reserve_screen();
}

const char *prog;

#define CMD(c) {.is_command = true, .cmd = c}

#define MAP(m) {.is_command = false, .nested = m}

struct keymap_entry {
        const char *k;
        struct {
                bool is_command;
                union {
                        void (*cmd)();
                        const struct keymap_entry *nested;
                };
        };
};

const struct keymap_entry extended_keymap[] = {
        {"=", CMD(show_line_column)},  {"C-c", CMD(quit)},
        {"C-n", CMD(set_goal_column)}, {"C-q", CMD(toggle_read_only_mode)},
        {"C-s", CMD(save_buffer)},     {"C-x", CMD(exchange_point_and_mark)},
        {"M-c", CMD(kill_ted)},        {0, CMD(cancel)},
};

const struct keymap_entry global_keymap[] = {
        {"C-<space>", CMD(set_mark)},
        {"C-a", CMD(beginning_of_row)},
        {"C-b", CMD(backward_char)},
        {"C-d", CMD(delete_char)},
        {"C-e", CMD(end_of_row)},
        {"C-f", CMD(forward_char)},
        {"C-n", CMD(next_row)},
        {"C-o", CMD(open_line)},
        {"C-p", CMD(previous_row)},
        {"C-q", CMD(search_quit)},
        {"C-r", CMD(search_previous)},
        {"C-s", CMD(search_buffer)},
        {"C-v", CMD(scroll_up)},
        {"C-w", CMD(kill_region)},
        {"C-x", MAP(extended_keymap)},
        {"C-y", CMD(yank)},
        {"C-z", CMD(suspend)},
        {"C-<down>", CMD(forward_paragraph)},
        {"C-<left>", CMD(backward_word)},
        {"C-<right>", CMD(forward_word)},
        {"C-<up>", CMD(backward_paragraph)},
        {"M-O", CMD(open_previous_line)},
        {"M-a", CMD(beginning_of_line)},
        {"M-b", CMD(backward_word)},
        {"M-e", CMD(end_of_line)},
        {"M-f", CMD(forward_word)},
        {"M-g", CMD(goto_line)},
        {"M-o", CMD(open_next_line)},
        {"M-v", CMD(scroll_down)},
        {"M-w", CMD(kill_region_save)},
        {"M-%", CMD(goto_percent)},
        {"M-<", CMD(beginning_of_buffer)},
        {"M->", CMD(end_of_buffer)},
        {"S-<down>", CMD(set_mark_next_row)},
        {"S-<left>", CMD(set_mark_backward_char)},
        {"S-<right>", CMD(set_mark_forward_char)},
        {"S-<up>", CMD(set_mark_previous_row)},
        {"C-M-b", CMD(backward_paragraph)},
        {"C-M-f", CMD(forward_paragraph)},
        {"C-S-<down>", CMD(set_mark_forward_paragraph)},
        {"C-S-<left>", CMD(set_mark_backward_word)},
        {"C-S-<right>", CMD(set_mark_forward_word)},
        {"C-S-<up>", CMD(set_mark_backward_paragraph)},
        {"<backspace>", CMD(delete_backward_char)},
        {"<delete>", CMD(delete_forward_char)},
        {"<down>", CMD(next_row)},
        {"<end>", CMD(end_of_row)},
        {"<home>", CMD(beginning_of_row)},
        {"<left>", CMD(backward_char)},
        {"<next>", CMD(page_down)},
        {"<prior>", CMD(page_up)},
        {"<right>", CMD(forward_char)},
        {"<up>", CMD(previous_row)},
        {0, CMD(cancel)},
};
#undef CMD
#undef MAP

void main_loop()
{
        char echo_buf[128];
        size_t n = 0;
        struct key k;
        bool is_keychord = false;

#define READ(k)                              \
        do {                                 \
                k = read_key();              \
                if (key_eq(k, kbd("C-g"))) { \
                        cancel();            \
                        goto start;          \
                }                            \
        } while (0)
start:
        while (1) {
                refresh();
                n = 0;
                is_keychord = false;
                ed.is_prefix = false;

                if (!ed.preserve_echo)
                        echo_clear();
                else
                        ed.preserve_echo = false;

                READ(k);
                if (key_eq(k, kbd("C-u"))) {
                        n += snprintf(echo_buf + n, 128 - n, "C-u ");
                        echo_info(echo_buf);

                        ed.is_prefix = true;
                        bool scanned_num = false;
                        ed.prefix_arg = 0;

                        READ(k);
                        while (is_digit(k)) {
                                scanned_num = true;
                                ed.prefix_arg = ed.prefix_arg * 10 + (k.u.c[0] - '0');
                                n += snprintf(echo_buf + n, 128 - n, "%c ", k.u.c[0]);
                                echo_info(echo_buf);

                                READ(k);
                        }

                        if (!scanned_num)
                                ed.prefix_arg = 1;
                }

                const struct keymap_entry *km = global_keymap;
                size_t i = 0;
                while (km[i].k) {
                        if (key_eq(k, kbd(km[i].k))) {
                                if (km[i].is_command) {
                                        ed.last_key = k;
                                        km[i].cmd();
                                        break;
                                } else {
                                        n += snprintf(echo_buf + n, 128 - n, "%s ", km[i].k);
                                        echo_info(echo_buf);
                                        km = km[i].nested;
                                        is_keychord = true;
                                        READ(k);
                                        i = 0;
                                }
                        } else {
                                ++i;
                        }
                }

                if (!km[i].k) {
                        if (is_textchar(k) && !is_keychord) {
                                if (ed.marks.is_active) {
                                        delete_region();
                                        disable_mark();
                                }

                                ed.last_key = k;
                                insert_char();
                        } else {
                                echo_error("Key is undefined.");
                        }
                }
        }
#undef READ
}

static void print_usage_and_exit()
{
        fprintf(stderr, "Usage: ted [OPTION] FILE\n");
        fprintf(stderr, "Edit FILE on the terminal.\n\n");
        fprintf(stderr, "  -c COLS\tShow COLS columns per screen line.\n");
        fprintf(stderr, "  -f unix|dos\tUse unix or dos line-endings.\n");
        fprintf(stderr, "  -g first\tStart with point at the beginning.\n");
        fprintf(stderr, "  -g last\tStart with point at the end.\n");
        fprintf(stderr, "  -g NUM\tStart with point at the NUMth character.\n");
        fprintf(stderr, "  -r ROWS\tShow ROWS lines at a time.\n");
        fprintf(stderr, "  -t TABS\tUse TABS columns for each tabstop.\n");
        exit(EXIT_FAILURE);
}

void editor_config_init(int argc, char *argv[])
{
        int c;
        char *endptr;
        long rows, cols, tabs;

        ed.nlines = DEFAULT_NLINES;
        ed.ncols = DEFAULT_NCOLS;
        ed.tabstop = DEFAULT_TABSTOP;
        ed.filetype = DEFAULT_FILETYPE;
        ed.options.position.k = FIRST;
        while ((c = getopt(argc, argv, "r:c:t:f:g:")) != -1) {
                switch (c) {
                case 'r':
                        if (!*optarg)
                                print_usage_and_exit();
                        rows = strtol(optarg, &endptr, 10);
                        if (*endptr)
                                print_usage_and_exit();
                        if (rows < MIN_NLINES || rows > MAX_NLINES)
                                print_usage_and_exit();
                        ed.nlines = rows;
                        break;
                case 'c':
                        if (!*optarg)
                                print_usage_and_exit();
                        cols = strtol(optarg, &endptr, 10);
                        if (*endptr)
                                print_usage_and_exit();
                        if (cols < MIN_NCOLS || cols > MAX_NCOLS)
                                print_usage_and_exit();
                        ed.ncols = cols;
                        break;
                case 't':
                        if (!*optarg)
                                print_usage_and_exit();
                        tabs = strtol(optarg, &endptr, 10);
                        if (*endptr)
                                print_usage_and_exit();
                        if (tabs < MIN_TABSTOP || tabs > MAX_TABSTOP)
                                print_usage_and_exit();
                        ed.tabstop = tabs;
                        break;
                case 'f':
                        if (!*optarg)
                                print_usage_and_exit();
                        if (!strcmp(optarg, "dos"))
                                ed.filetype = DOS;
                        else if (!strcmp(optarg, "unix"))
                                ed.filetype = UNIX;
                        else
                                print_usage_and_exit();
                        break;
                case 'g':
                        if (!*optarg)
                                print_usage_and_exit();
                        if (!strcmp(optarg, "first")) {
                                ed.options.position.k = FIRST;
                        } else if (!strcmp(optarg, "last")) {
                                ed.options.position.k = LAST;
                        } else {
                                char *endptr;

                                ed.options.position.k = OFFSET;
                                ed.options.position.offset = strtoul(optarg, &endptr, 10);
                                if (*endptr)
                                        print_usage_and_exit();
                        }
                }
        }
}

int main(int argc, char *argv[])
{
        prog = argv[0];

        if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
                err_exit("stdin and stdout should be tty.\n");

        editor_config_init(argc, argv);

        if (optind >= argc)
                print_usage_and_exit();

        loadf(argv[optind]);

        terminal_setup();

        reserve_screen();

        refresh();

        switch (ed.options.position.k) {
        case FIRST:
                break;
        case LAST:
                end_of_buffer();
                break;
        case OFFSET:
                move_to(ed.options.position.offset);
                break;
        default:
                unreachable();
        }

        main_loop();

        return 0;
}
