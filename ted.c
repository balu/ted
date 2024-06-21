#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
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

#define NLINES (10)
#define NCOLS (80)
#define TABSTOP (8)

#define CONTINUATION_LINE_STR "\x1b[31m\\\x1b[m"
#define EMPTY_LINE_STR "\x1b[34m~\x1b[m"

#define BUFSIZE (1024 * 1024)

#define BLKSIZE (4096)

#define SCREENBUF_SIZE (NLINES * (NCOLS + 1) * 4)

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

        k.shift = (k.m - 1) & 0x1;
        k.meta = (k.m - 1) & 0x2;
        k.ctrl = (k.m - 1) & 0x4;
        k.super = (k.m - 1) & 0x8;

        return k;
}

struct key scan_escape(uint8_t buf[])
{
        struct key k = {0};

        if (!buf[0]) {
                k.special = ESC;
                return k;
        }

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
}

struct key read_key()
{
        struct key k = {0};
        uint8_t buf[16] = {0};

        ssize_t nread = read(STDIN_FILENO, buf, sizeof(buf));
        assert(nread > 0);
        buf[nread] = 0;

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

        if (s[0] && s[0] == 'C' && s[1] && s[1] == '-') {
                k->super = 1;
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

enum cursor_type {
        DEFAULT = 0,
        BLINKING_BLOCK = 1,
        STEADY_BLOCK = 2,
        BLINKING_UNDERLINE = 3,
        STEADY_UNDERLINE = 4,
        BLINKING_BAR = 5,
        STEADY_BAR = 6,
};

void set_cursor_type(enum cursor_type type)
{
        char buf[8] = {0};

        sprintf(buf, "\x1b[%d q", type);
        write(STDOUT_FILENO, buf, strlen(buf));
}

void err_exit(const char *message)
{
        if (errno)
                perror(message);
        else
                fprintf(stderr, message);

        exit(1);
}

struct termios old_termios;

void terminal_reset()
{
        tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
        set_cursor_type(DEFAULT);
}

void terminal_setup()
{
        struct termios new_termios;

        if (tcgetattr(STDIN_FILENO, &old_termios) == -1)
                err_exit("terminal_setup: tcgetattr() failed");

        cfmakeraw(&new_termios);

        if (tcsetattr(STDIN_FILENO, TCSANOW, &new_termios) == -1)
                err_exit("terminal_setup: tcsetattr() failed");

        set_cursor_type(BLINKING_BAR);

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

        printf(buf);
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
        struct position screen_begin;
        struct position echo_begin;
        bool is_prefix;
        int prefix_arg;

        size_t tabstop;
        enum { UNIX, DOS } filetype;
        bool ensure_trailing_newline;
        const char *filename;
        struct tedchar buffer[BUFSIZE];
        struct tedchar *gap_start;
        struct tedchar *gap_end;
        struct tedchar *tl;
        size_t cursor_row;
        size_t cursor_col;
        size_t goal_col;
        bool force_goal_col;
        size_t mark;
        bool is_mark_active;
        bool is_selection_active;
        bool is_dirty;
        struct key last_key;
        bool is_error;
        bool is_info;
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
                switch (buf[i]) {
                case 0x20 ... 0x7e:
                        ++i;
                        break;
                default:
                        buf[i] = '?';
                        ++i;
                }
        }
        buf[ed.ncols] = 0;
}

void echo(const char *message, ...)
{
        char buf[512];
        va_list ap;

        va_start(ap, message);
        vsnprintf(buf, sizeof(buf), message, ap);
        sanitize(buf);
        size_t n = strlen(buf);
        strcpy(buf + n, "\x1b[K");

        save_cursor();

        goto_(ed.echo_begin);
        write(STDOUT_FILENO, buf, strlen(buf));

        restore_cursor();
}

void echo_error(const char *message, ...)
{
        char buf[512];
        va_list ap;

        va_start(ap, message);

        size_t n = snprintf(buf, sizeof(buf), "\x1b[31m\x1b[1m");
        n += vsnprintf(buf + n, sizeof(buf) - n, message, ap);
        n += snprintf(buf + n, sizeof(buf) - n, "\x1b[m\x1b[K");

        save_cursor();

        goto_(ed.echo_begin);
        write(STDOUT_FILENO, buf, strlen(buf));

        restore_cursor();

        ed.is_error = true;
}

void echo_info(const char *message, ...)
{
        char buf[512];
        va_list ap;

        va_start(ap, message);

	size_t n = snprintf(buf, sizeof(buf), "\x1b[33m");
        n += vsnprintf(buf + n, sizeof(buf) - n, message, ap);
        n += snprintf(buf + n, sizeof(buf) - n, "\x1b[m\x1b[K");

        save_cursor();

        goto_(ed.echo_begin);
        write(STDOUT_FILENO, buf, strlen(buf));

        restore_cursor();

        ed.is_info = true;
}


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
	ed.is_mark_active = false;
	ed.is_selection_active = false;
}

void loadf(const char *filename)
{
        int fd;

        if ((fd = open(filename, O_RDONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0) {
                perror("loadf: Failed to open file");
                goto err3;
        }

        struct stat st;

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

        ed.filetype = UNIX; // TODO: Detect automatically, allow user to change.

        n = tedchar_from_bytes(ed.buffer, BUFSIZE, buf, st.st_size);

        free(buf);

        ed.filename = filename;

        ed.ensure_trailing_newline = true;

        ed.gap_start = ed.buffer + n;
        ed.gap_end = ed.buffer + BUFSIZE;
        ed.cursor_row = 0;
        ed.cursor_col = 0;
        ed.goal_col = 0;
        ed.tl = NULL;

	disable_mark();

        ed.is_dirty = false;

        ed.is_error = false;
        ed.is_info = false;

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

void point_mark_low_high(size_t *low, size_t *high)
{
        size_t p = is_point_at_end_of_buffer() ? buffer_size() : index_of(char_at_point());
        size_t m = ed.mark;

        *low = p <= m ? p : m;
        *high = p <= m ? m : p;
}

void refresh()
{
        hide_cursor();

        screenbuf_init();

        size_t low = 0, high = 0;

        if (ed.is_selection_active) {
                point_mark_low_high(&low, &high);
        }

        bool highlight_active = false;

        struct tedchar *current = ed.tl;

        for (size_t lines = 0; lines < ed.nlines; ++lines) {
                size_t col = 0;
                bool line = false;
                bool newline = false;

                while (current) {
                        if (ed.is_selection_active && !highlight_active &&
                            index_of(current) >= low && index_of(current) < high) {
                                highlight_on();
                                highlight_active = true;
                        }

                        if (ed.is_selection_active && highlight_active &&
                            index_of(current) == high) {
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

void scroll_up()
{
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

void forward_char()
{
        struct tedchar *c = char_at_point();

        if (!c)
                return;

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

void backward_char()
{
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

void forward_word()
{
        while (!is_buffer_empty() && !is_point_at_end_of_buffer() &&
               is_whitespace(*char_at_point()))
                forward_char();

        while (!is_buffer_empty() && !is_point_at_end_of_buffer() &&
               !is_whitespace(*char_at_point()))
                forward_char();
}

void backward_word()
{
        if (is_buffer_empty() || is_point_at_beginning_of_buffer())
                return;

        backward_char();

        while (!is_point_at_beginning_of_buffer() && is_whitespace(*char_at_point()))
                backward_char();

        bool found_word = false;
        while (1) {
                if (is_whitespace(*char_at_point())) {
                        if (found_word)
                                forward_char();
                        break;
                }

                if (is_point_at_beginning_of_buffer())
                        break;

                found_word = true;

                backward_char();
        }
}

void forward_paragraph()
{
        if (is_buffer_empty() || is_point_at_end_of_buffer())
                return;

        while (!is_point_at_end_of_buffer() && is_whitespace(*char_at_point()))
                forward_char();

        size_t newline_run = 0;
        while (!is_point_at_end_of_buffer()) {
                if (is_newline(*char_at_point())) {
                        ++newline_run;
                        if (newline_run == 2)
                                return;
                } else {
                        newline_run = 0;
                }

                forward_char();
        }
}

void backward_paragraph()
{
        if (is_buffer_empty() || is_point_at_beginning_of_buffer())
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
                                return;
                        }
                } else {
                        newline_run = 0;
                }

                backward_char();
        }
}

void next_row()
{
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
                        return;
                }
                forward_char();
        }
}

void previous_row()
{
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
                        return;
                }

                forward_char();
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

        beginning_of_buffer();
        while (--line_no) {
                end_of_line();
                forward_char();
        }
}

void goto_percent()
{
        size_t percent = ed.prefix_arg;

        if (!ed.is_prefix || ed.prefix_arg < 0)
                percent = 0;
        else if (ed.prefix_arg > 100)
                percent = 100;

        size_t total = buffer_size();

        size_t n = (total * percent) / 100;

        beginning_of_buffer();
        while (n--)
                forward_char();
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
        for (size_t i = 0; i < (ed.nlines + 2) / 2; ++i) {
                scroll_up();
                next_row();
        }
}

void page_up()
{
        for (size_t i = 0; i < (ed.nlines + 2) / 2; ++i) {
                scroll_down();
                previous_row();
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
        } else {
                assert(0);
        }
}

void insert_char()
{
        struct key k = ed.last_key;

        assert(is_textchar(k));

        struct tedchar t;

        if (key_eq(k, kbd("<cr>")))
                t = tedchar_newline();
        else if (key_eq(k, kbd("<tab>")))
                t = tedchar_utf8(utf8_ascii('\t'));
        else
                t = tedchar_utf8(k.u);

        do_insert_char(t);
}

void open_line()
{
        do_insert_char(tedchar_newline());
        backward_char();
}

void open_next_line()
{
	end_of_line();
	do_insert_char(tedchar_newline());
}

void open_previous_line()
{
	beginning_of_line();
	open_line();
}

void delete_char()
{
        if (is_buffer_empty() || is_point_at_end_of_buffer())
                return;

        ed.is_dirty = true;

        if (ed.cursor_row == ed.nlines - 1 && next_col(*char_at_point(), ed.cursor_col) == 0)
                scroll_up();

        if (ed.tl == ed.gap_end) {
                ed.tl = advance(ed.gap_end);
        }

        ++ed.gap_end;
}

void delete_region()
{
	size_t low, high;

	point_mark_low_high(&low, &high);

	struct tedchar *p = char_at_index(low);

	if (!p) return;

	size_t nchars = high-low;

	if (nchars == 0) return;

	ed.is_dirty = true;

	beginning_of_buffer();
	while (low--)
		forward_char();

	while (nchars--)
		delete_char();
}

void delete_backward_char()
{
	if (ed.is_selection_active) {
		delete_region();
		disable_mark();
		return;
	}

        if (is_buffer_empty() || is_point_at_beginning_of_buffer())
                return;

        ed.is_dirty = true;

        backward_char();
        delete_char();
}

void delete_forward_char()
{
	if (ed.is_selection_active) {
		delete_region();
		disable_mark();
		return;
	}

	delete_char();
}

void save_buffer()
{
        if (ed.ensure_trailing_newline && !is_buffer_empty()) {
                struct tedchar *p = char_at_index(buffer_size() - 1);
                if (!is_newline(*p)) {
                        if (is_point_at_end_of_buffer()) {
                                do_insert_char(tedchar_newline());
                        } else {
                                size_t where = index_of(char_at_point());

                                end_of_buffer();
                                do_insert_char(tedchar_newline());

                                beginning_of_buffer();
                                while (where--)
                                        forward_char();
                        }
                }
        }

        int fd = open(ed.filename, O_CREAT | O_TRUNC | O_WRONLY);

        uint8_t buf[BLKSIZE] = {0};
        size_t i = 0;

        struct tedchar *p = first_char();

        while (1) {
                while (p) {
                        if (is_newline(*p)) {
                                if (ed.filetype == UNIX) {
                                        if (i >= BLKSIZE)
                                                break;

                                        buf[i++] = '\n';
                                } else if (ed.filetype == DOS) {
                                        if (i >= BLKSIZE - 1)
                                                break;

                                        buf[i++] = '\r';
                                        buf[i++] = '\n';
                                }
                        } else {
                                size_t k = utf8_count(p->u.c);

                                if (i + k >= BLKSIZE)
                                        break;

                                for (size_t j = 0; j < k; ++j) {
                                        buf[i++] = p->u.c[j];
                                }
                        }
                        p = advance(p);
                }

                write(fd, buf, i);
                i = 0;
                if (!p)
                        break;
        }

        close(fd);

        ed.is_dirty = false;
}

void set_mark()
{
        ed.mark = is_point_at_end_of_buffer() ? buffer_size() : index_of(char_at_point());
        ed.is_mark_active = true;
        ed.is_selection_active = true;
}

void exchange_point_and_mark()
{
        if (is_buffer_empty())
                return;

        if (!ed.is_mark_active)
                return;

        size_t save_mark = ed.mark;
        ed.mark = is_point_at_end_of_buffer() ? buffer_size() : index_of(char_at_point());

        beginning_of_buffer();
        while (save_mark--)
                forward_char();
}

void kill_region_save()
{
        if (!ed.is_selection_active)
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

        ed.is_selection_active = false;
}

void kill_region()
{
        if (!ed.is_selection_active)
                return;

        kill_region_save();

        size_t low, high;

        point_mark_low_high(&low, &high);

        size_t nchars = high - low;

        beginning_of_buffer();
        for (size_t k = 0; k < low; ++k)
                forward_char();
        while (nchars--)
                delete_char();

        ed.is_selection_active = false;
}

void yank()
{
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

	echo_info("L%uC%u", line_no, col_no);
}

void quit()
{
        if (ed.is_dirty) {
                if (ed.is_prefix) {
                        save_buffer();
                        emit_clear_screen();
                        exit(0);
                }

                echo_error("Save and quit: C-u C-x C-c. Quit without saving: C-x M-c.");
                return;
        }

        emit_clear_screen();
        exit(0);
}

void kill_ted()
{
        emit_clear_screen();
        exit(1);
}

void cancel()
{
        ed.is_selection_active = false;
        echo("");
}

const char *prog;

#define CMD(c)                               \
        {                                    \
                .is_command = true, .cmd = c \
        }

#define MAP(m)                                   \
        {                                        \
                .is_command = false, .nested = m \
        }

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
	{"=", CMD(show_line_column)},
        {"C-c", CMD(quit)},        {"C-n", CMD(set_goal_column)},
        {"C-s", CMD(save_buffer)}, {"C-x", CMD(exchange_point_and_mark)},
        {"M-c", CMD(kill_ted)},    {0, CMD(cancel)},
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
        {"C-v", CMD(scroll_up)},
        {"C-w", CMD(kill_region)},
        {"C-x", MAP(extended_keymap)},
        {"C-y", CMD(yank)},
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
        {"C-M-b", CMD(backward_paragraph)},
        {"C-M-f", CMD(forward_paragraph)},
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

                if (!ed.is_error && !ed.is_info) {
                        echo("");
		} else {
                        ed.is_error = false;
			ed.is_info = false;
		}

                READ(k);
                if (key_eq(k, kbd("C-u"))) {
                        n += snprintf(echo_buf + n, 128 - n, "C-u ");
                        echo(echo_buf);

                        ed.is_prefix = true;
                        bool scanned_num = false;
                        ed.prefix_arg = 0;

                        READ(k);
                        while (is_digit(k)) {
                                scanned_num = true;
                                ed.prefix_arg = ed.prefix_arg * 10 + (k.u.c[0] - '0');
                                n += snprintf(echo_buf + n, 128 - n, "%c ", k.u.c[0]);
                                echo(echo_buf);

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
                                        echo(echo_buf);
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
                                ed.last_key = k;
                                insert_char();
                        } else {
                                echo_error("Undefined key");
                        }
                }
        }
#undef READ
}

void editor_config_init()
{
        ed.nlines = NLINES;
        ed.ncols = NCOLS;
        ed.tabstop = TABSTOP;
}

int main([[maybe_unused]] int argc, const char *argv[])
{
        prog = argv[0];

        if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
                err_exit("stdin and stdout should be tty.\n");

        if (argc < 2)
                err_exit("Usage: ted FILE\n");

        editor_config_init();

        loadf(argv[1]);

        terminal_setup();

        reserve_screen();

        main_loop();

        return 0;
}
