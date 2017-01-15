#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <sys/mman.h>

#define CONFIG_ENVVAR   "VEXRC"
#define CONFIG_USERFILE ".vexrc"
#define CONFIG_SYSFILE  "/etc/vexrc"

/* formatting constants {{{ */
#define GUTTER 5
/* }}} */
/* TYPES {{{ */
typedef struct {
	char *layout;
	char *status;
} CONFIG;

typedef void (*prcell_fn)(WINDOW *w, uint8_t v);
typedef struct {
	WINDOW     *win;
	prcell_fn   pr;
	int         width; /* cell width, in printable columns */
} COLUMN;

typedef void (*fmt_fn)(void *, int, void *);
typedef struct {
	fmt_fn  fmt;
	int     width;
	char   *literal;
} FIELD;

typedef struct {
	COLUMN *columns; /* column views (hex, octal, etc.) */
	int width;       /* column width, in cells/octets */
	int ncol;        /* how many columns are there? */

	FIELD *fields;
	int nfields;
	WINDOW *status;  /* the status bar window (for drawing) */

	WINDOW *command; /* the command window (errors, search, etc. */

	const char *path;
	const char *file;

	int main_height; /* height of main editor pane, in rows */
	int st_height;   /* height of the status bar, in rows */

	uint8_t *data;   /* the data mmap pointer */
	size_t len;      /* how much data is there? */
	size_t offset;   /* offset (to data) of first printed octet */
	int pos;         /* cursor position, counting from l->offset */
} LAYOUT;
/* }}} */
/* utility functions {{{ */
#define max(a,b) ((a) > (b) ? (a) : (b))
#define min(a,b) ((a) < (b) ? (a) : (b))
#define DATA_AT(l,plus) (DATA(l) + (plus))
#define DATA(l) ((l)->data + (l)->offset)
#define as(t,x) (*(t *)(x))
#define as_u8(x)  as(uint8_t,  x)
#define as_i8(x)  as(int8_t,   x)
#define as_u16(x) as(uint16_t, x)
#define as_i16(x) as(int16_t,  x)
#define as_u32(x) as(uint32_t, x)
#define as_i32(x) as(int32_t,  x)
#define as_u64(x) as(uint64_t, x)
#define as_i64(x) as(int64_t,  x)
#define as_f32(x) as(float,    x)
#define as_f64(x) as(double,   x)

int pop8(uint8_t x)
{
	x = (x & 0x55) + ((x >> 1) & 0x55);
	x = (x & 0x33) + ((x >> 2) & 0x33);
	x = (x & 0x0f) + ((x >> 4) & 0x0f);
	return x;
}
int pop16(uint16_t x)
{
	x = (x & 0x5555) + ((x >> 1) & 0x5555);
	x = (x & 0x3333) + ((x >> 2) & 0x3333);
	x = (x & 0x0f0f) + ((x >> 4) & 0x0f0f);
	x = (x & 0x00ff) + ((x >> 8) & 0x00ff);
	return x;
}
int pop32(uint32_t x)
{
	x = (x & 0x55555555) + ((x >>  1) & 0x55555555);
	x = (x & 0x33333333) + ((x >>  2) & 0x33333333);
	x = (x & 0x0f0f0f0f) + ((x >>  4) & 0x0f0f0f0f);
	x = (x & 0x0f0f0f0f) + ((x >>  8) & 0x0f0f0f0f);
	x = (x & 0x00ff00ff) + ((x >> 16) & 0x00ff00ff);
	return x;
}
int pop64(uint64_t x)
{
	x = (x & 0x5555555555555555) + ((x >>  1) & 0x5555555555555555);
	x = (x & 0x3333333333333333) + ((x >>  2) & 0x3333333333333333);
	x = (x & 0x0f0f0f0f0f0f0f0f) + ((x >>  4) & 0x0f0f0f0f0f0f0f0f);
	x = (x & 0x00ff00ff00ff00ff) + ((x >>  8) & 0x00ff00ff00ff00ff);
	x = (x & 0x0000ffff0000ffff) + ((x >> 16) & 0x0000ffff0000ffff);
	x = (x & 0x00000000ffffffff) + ((x >> 32) & 0x00000000ffffffff);
	return x;
}

int clz8(uint8_t x)
{
	uint8_t y;
	int n;

	n = 8;
	y = x >> 4; if (y) { n -= 4; x = y; }
	y = x >> 2; if (y) { n -= 2; x = y; }
	y = x >> 1; if (y) return n - 2;
	return n - x;
}
int clz16(uint16_t x)
{
	uint16_t y;
	int n;

	n = 16;
	y = x >> 8; if (y) { n -= 8; x = y; }
	y = x >> 4; if (y) { n -= 4; x = y; }
	y = x >> 2; if (y) { n -= 2; x = y; }
	y = x >> 1; if (y) return n - 2;
	return n - x;
}
int clz32(uint32_t x)
{
	uint32_t y;
	int n;

	n = 32;
	y = x >> 16; if (y) { n -= 16; x = y; }
	y = x >>  8; if (y) { n -=  8; x = y; }
	y = x >>  4; if (y) { n -=  4; x = y; }
	y = x >>  2; if (y) { n -=  2; x = y; }
	y = x >>  1; if (y) return n - 2;
	return n - x;
}
int clz64(uint64_t x)
{
	uint64_t y;
	int n;

	n = 64;
	y = x >> 32; if (y) { n -= 32; x = y; }
	y = x >> 16; if (y) { n -= 16; x = y; }
	y = x >>  8; if (y) { n -=  8; x = y; }
	y = x >>  4; if (y) { n -=  4; x = y; }
	y = x >>  2; if (y) { n -=  2; x = y; }
	y = x >>  1; if (y) return n - 2;
	return n - x;
}

#define ctz8(x)  (8  - clz8 (~(x) & ((x)-1)))
#define ctz16(x) (16 - clz16(~(x) & ((x)-1)))
#define ctz32(x) (32 - clz32(~(x) & ((x)-1)))
#define ctz64(x) (64 - clz64(~(x) & ((x)-1)))

static void anyexit(int rc)
{
	printw("press any key to exit...");
	refresh();
	getch();
	endwin();
	exit(rc);
}

int cfgcol(LAYOUT *l, COLUMN *c, prcell_fn pr, int x, int width, int space)
{
	c->pr    = pr;
	c->width = width;
	c->win   = newwin(LINES, c->width * l->width, 0, x);
	return c->width * l->width + GUTTER - space;
}

/* }}} */
/* colors {{{ */
#define C_NORMAL_IDX 1
#define C_NORMAL COLOR_PAIR(C_NORMAL_IDX)

#define C_CURSOR_IDX 2
#define C_CURSOR COLOR_PAIR(C_CURSOR_IDX)

#define C_STATUS_IDX 3
#define C_STATUS COLOR_PAIR(C_STATUS_IDX) | A_BOLD

#define C_ERROR_IDX 4
#define C_ERROR COLOR_PAIR(C_ERROR_IDX) | A_BOLD

static void the_colors()
{
	start_color();
	init_pair(C_NORMAL_IDX, COLOR_WHITE, COLOR_BLACK);
	init_pair(C_CURSOR_IDX, COLOR_BLACK, COLOR_WHITE);
	init_pair(C_STATUS_IDX, COLOR_GREEN, COLOR_BLACK);
	init_pair(C_ERROR_IDX,  COLOR_WHITE, COLOR_RED);
}
/* }}} */

static void pr_ascii(WINDOW *w, uint8_t v) /* {{{ */
{
	if (v < 32 || v > 126) {
		waddch(w, '.');
	} else {
		waddch(w, v);
	}
}
/* }}} */
static void pr_hex(WINDOW *w, uint8_t v) /* {{{ */
{
	wprintw(w, "%02x", v);
	wattroff(w, C_CURSOR);
	waddch(w, ' ');
}
/* }}} */
static void pr_hex_pretty(WINDOW *w, uint8_t v) /* {{{ */
{
	if (v == 0) {
		waddch(w, '-');
		waddch(w, ' ');
	} else {
		wattron(w, A_BOLD);
		wprintw(w, "%02x", v);
		wattroff(w, A_BOLD);
	}
	wattroff(w, C_CURSOR);
	waddch(w, ' ');
}
/* }}} */
static void pr_oct(WINDOW *w, uint8_t v) /* {{{ */
{
	wprintw(w, "% 3o", v);
	wattroff(w, C_CURSOR);
	waddch(w, ' ');
}
/* }}} */
static void pr_oct_pretty(WINDOW *w, uint8_t v) /* {{{ */
{
	if (v == 0) {
		waddch(w, ' ');
		waddch(w, '-');
		waddch(w, ' ');
	} else {
		wattron(w, A_BOLD);
		wprintw(w, "% 3o", v);
		wattroff(w, A_BOLD);
	}
	wattroff(w, C_CURSOR);
	waddch(w, ' ');
}
/* }}} */

static void fmt_literal(void *l, int width, void *_field) /* {{{ */
{
	wprintw(((LAYOUT *)l)->status, "%s", ((FIELD*)_field)->literal);
} /* }}} */
static void fmt_ud(void *_, int width, void *_field) /* {{{ */
{
	int left;
	LAYOUT *l;

	l = (LAYOUT*)_;
	left = l->len - (l->offset + l->pos);

	switch (width) {
	case 8:
		wprintw(l->status, "% 3u", as_u8(DATA_AT(l, l->pos)));
		break;

	case 16:
		if (left >= 2) wprintw(l->status, "% 6u", as_u16(DATA_AT(l, l->pos)));
		else           wprintw(l->status, "% 6s", "");
		break;
	case 32:
		if (left >= 4) wprintw(l->status, "% 11u", as_u32(DATA_AT(l, l->pos)));
		else           wprintw(l->status, "% 11s", "");
		break;

	case 64:
		if (left >= 8) wprintw(l->status, "% 20u", as_u64(DATA_AT(l, l->pos)));
		else           wprintw(l->status, "% 20s", "");
		break;

	default:
		wprintw(l->status, "!!!");
		break;
	}
} /* }}} */
static void fmt_sd(void *_, int width, void *_field) /* {{{ */
{
	int left;
	LAYOUT *l;

	l = (LAYOUT*)_;
	left = l->len - (l->offset + l->pos);

	switch (width) {
	case 8:
		wprintw(l->status, "% 3i", as_i8(DATA_AT(l, l->pos)));
		break;

	case 16:
		if (left >= 2) wprintw(l->status, "% 6i", as_i16(DATA_AT(l, l->pos)));
		else           wprintw(l->status, "% 6s", "");
		break;
	case 32:
		if (left >= 4) wprintw(l->status, "% 11i", as_i32(DATA_AT(l, l->pos)));
		else           wprintw(l->status, "% 11s", "");
		break;

	case 64:
		if (left >= 8) wprintw(l->status, "% 20i", as_i64(DATA_AT(l, l->pos)));
		else           wprintw(l->status, "% 20s", "");
		break;

	default:
		wprintw(l->status, "!!!");
		break;
	}
} /* }}} */
static void fmt_lz(void *_, int width, void *_field) /* {{{ */
{
	int left;
	LAYOUT *l;

	l = (LAYOUT*)_;
	left = l->len - (l->offset + l->pos);

	switch (width) {
	case 8:
		wprintw(l->status, "%i", clz8(as_u8(DATA_AT(l, l->pos))));
		break;
	case 16:
		if (left >= 2) wprintw(l->status, "% 2i", clz16(as_u16(DATA_AT(l, l->pos))));
		else           wprintw(l->status, "  ");
		break;
	case 32:
		if (left >= 4) wprintw(l->status, "% 2i", clz32(as_u32(DATA_AT(l, l->pos))));
		else           wprintw(l->status, "  ");
		break;
	case 64:
		if (left >= 4) wprintw(l->status, "% 2i", clz64(as_u64(DATA_AT(l, l->pos))));
		else           wprintw(l->status, "  ");
		break;
	default:
		wprintw(l->status, "!!!");
		break;
	}
} /* }}} */
static void fmt_tz(void *_, int width, void *_field) /* {{{ */
{
	int left;
	LAYOUT *l;

	l = (LAYOUT*)_;
	left = l->len - (l->offset + l->pos);

	switch (width) {
	case 8:
		wprintw(l->status, "%i", ctz8(as_u8(DATA_AT(l, l->pos))));
		break;
	case 16:
		if (left >= 2) wprintw(l->status, "% 2i", ctz16(as_u16(DATA_AT(l, l->pos))));
		else           wprintw(l->status, "  ");
		break;
	case 32:
		if (left >= 4) wprintw(l->status, "% 2i", ctz32(as_u32(DATA_AT(l, l->pos))));
		else           wprintw(l->status, "  ");
		break;
	case 64:
		if (left >= 4) wprintw(l->status, "% 2i", ctz64(as_u64(DATA_AT(l, l->pos))));
		else           wprintw(l->status, "  ");
		break;
	default:
		wprintw(l->status, "!!!");
		break;
	}
} /* }}} */
static void fmt_p(void *_, int width, void *_field) /* {{{ */
{
	int left;
	LAYOUT *l;

	l = (LAYOUT*)_;
	left = l->len - (l->offset + l->pos);

	switch (width) {
	case 8:
		wprintw(l->status, "%i", pop8(as_u8(DATA_AT(l, l->pos))));
		break;
	case 16:
		if (left >= 2) wprintw(l->status, "% 2i", pop16(as_u16(DATA_AT(l, l->pos))));
		else           wprintw(l->status, "  ");
		break;
	case 32:
		if (left >= 4) wprintw(l->status, "% 2i", pop32(as_u32(DATA_AT(l, l->pos))));
		else           wprintw(l->status, "  ");
		break;
	case 64:
		if (left >= 4) wprintw(l->status, "% 2i", pop64(as_u64(DATA_AT(l, l->pos))));
		else           wprintw(l->status, "  ");
		break;
	default:
		wprintw(l->status, "!!!");
		break;
	}
} /* }}} */
static void fmt_b(void *_, int width, void *_field) /* {{{ */
{
	int i;
	uint64_t v;
	LAYOUT *l;

	l = (LAYOUT *)_;

	switch (width) {
	case 8:
	case 16:
	case 32:
	case 64: break;
	default:
		wprintw(l->status, "!!!");
		return;
	}

	for (i = 0; i < width / 8; i++) {
		v = as_u8(DATA_AT(l, l->pos + i));
		if (i != 0) waddch(l->status, ' ');
		waddch(l->status, (v & 0x80) ? '1' : '0');
		waddch(l->status, (v & 0x40) ? '1' : '0');
		waddch(l->status, (v & 0x20) ? '1' : '0');
		waddch(l->status, (v & 0x10) ? '1' : '0');
		waddch(l->status, ' ');
		waddch(l->status, (v & 0x08) ? '1' : '0');
		waddch(l->status, (v & 0x04) ? '1' : '0');
		waddch(l->status, (v & 0x02) ? '1' : '0');
		waddch(l->status, (v & 0x01) ? '1' : '0');
	}
} /* }}} */
static void fmt_E(void *_, int width, void *_field) /* {{{ */
{
	LAYOUT *l;
	uint8_t buf[2] = { 0xba, 0xab };

	l = (LAYOUT *)_;
	if (as_u16(buf) == 0xbaab) {
		     if (width == 1) wprintw(l->status, "B");
		else if (width == 2) wprintw(l->status, "BE");
		else if (width == 3) wprintw(l->status, "big");
		else if (width == 0) wprintw(l->status, "big-endian");
		else                 wprintw(l->status, "!!!");
	} else {
		     if (width == 1) wprintw(l->status, "L");
		else if (width == 2) wprintw(l->status, "LE");
		else if (width == 3) wprintw(l->status, "lil");
		else if (width == 0) wprintw(l->status, "little-endian");
		else                 wprintw(l->status, "!!!");
	}
} /* }}} */
static void fmt_o(void *_, int width, void *_field) /* {{{ */
{
	LAYOUT *l;

	l = (LAYOUT *)_;
	wprintw(l->status, "%ld", l->offset + l->pos);
} /* }}} */
static void fmt_l(void *_, int width, void *_field) /* {{{ */
{
	LAYOUT *l;

	l = (LAYOUT *)_;
	wprintw(l->status, "%ld", l->len);
} /* }}} */
static void fmt_F(void *_, int width, void *_field) /* {{{ */
{
	LAYOUT *l;

	l = (LAYOUT *)_;
	wprintw(l->status, "%s", l->file);
} /* }}} */
static void fmt_P(void *_, int width, void *_field) /* {{{ */
{
	LAYOUT *l;

	l = (LAYOUT *)_;
	wprintw(l->status, "%s", l->path);
} /* }}} */
static void fmt_T(void *_, int width, void *_field) /* {{{ */
{
	int left, i;
	LAYOUT *l;
	char *s, *p;
	time_t t;

	l = (LAYOUT *)_;
	left = l->len - (l->offset + l->pos);

	if (left >= 4) {
		//Wed Jun 30 21:49:08 1993\n
		t = as_u32(DATA_AT(l, l->pos));
		s = ctime(&t);
		if (s) {
			p = strchr(s, '\n');
			if (p) *p = '\0';
			wprintw(l->status, "%24s", s);
		} else {
			wprintw(l->status, "%24s", "-");
		}
	}
	for (i = 0; i < width; i++) {
		if (i != 0) waddch(l->status, ' ');
		if (i > left) {
			waddch(l->status, ' ');
			waddch(l->status, ' ');
		} else {
			wprintw(l->status, "%02x", as_u8(DATA_AT(l, l->pos + i)));
		}
	}
} /* }}} */
static void fmt_x(void *_, int width, void *_field) /* {{{ */
{
	int left, i;
	LAYOUT *l;

	l = (LAYOUT *)_;
	left = l->len - (l->offset + l->pos);

	for (i = 0; i < width; i++) {
		if (i != 0) waddch(l->status, ' ');
		if (i > left) {
			waddch(l->status, ' ');
			waddch(l->status, ' ');
		} else {
			wprintw(l->status, "%02x", as_u8(DATA_AT(l, l->pos + i)));
		}
	}
} /* }}} */
static void fmt_f(void *_, int width, void *_field) /* {{{ */
{
	int left;
	LAYOUT *l;

	l = (LAYOUT *)_;
	left = l->len - (l->offset + l->pos);

	switch (width) {
	case 32:
		if (left >= 4) wprintw(l->status, "%f", as_f32(DATA_AT(l, l->pos)));
		else           waddch(l->status, '-');
		break;
	case 64:
		if (left >= 4) wprintw(l->status, "%lf", as_f64(DATA_AT(l, l->pos)));
		else           waddch(l->status, '-');
		break;
	default:
		wprintw(l->status, "!!!");
		break;
	}
} /* }}} */
static void fmt_e(void *_, int width, void *_field) /* {{{ */
{
	int left;
	LAYOUT *l;

	l = (LAYOUT *)_;
	left = l->len - (l->offset + l->pos);

	switch (width) {
	case 32:
		if (left >= 4) wprintw(l->status, "%e", as_f32(DATA_AT(l, l->pos)));
		else           waddch(l->status, '-');
		break;
	case 64:
		if (left >= 4) wprintw(l->status, "%le", as_f64(DATA_AT(l, l->pos)));
		else           waddch(l->status, '-');
		break;
	default:
		wprintw(l->status, "!!!");
		break;
	}
} /* }}} */

int parse_status(const char *s, FIELD *fields)
{
	int nfields, w;
	const char *a, *b;

	nfields = 0;
	a = s;
	while (*a) { /* until we run out of string ... */
		b = strchr(a, '%');
		if (!b) {
			/* trailing string literal */
			if (fields) {
				fields[nfields].fmt     = fmt_literal;
				fields[nfields].width   = -1;
				fields[nfields].literal = strdup(a);
			}
			nfields++;
			return nfields;
		}
		if (a != b) {
			if (fields) {
				fields[nfields].fmt     = fmt_literal;
				fields[nfields].width   = -1;
				fields[nfields].literal = calloc(b - a + 1, sizeof(char));
				memcpy(fields[nfields].literal, a, b - a);
			}
			nfields++;
		}

		b++;
		if (*b == '%') { /* '%%' */
			a = b + 1;
			continue;
		}

		w = 0;
		while (isdigit(*b)) {
			w = w * 10 + (*b) - '0';
			b++;
		}
		if (fields) fields[nfields].width = w;

		switch (*b) {
		case 'u':
			b++;
			if (*b != 'd') {
				printw("invalid format code '%%%du%c'\n", w, *b);
				return -1;
			}
			if (fields) fields[nfields].fmt = fmt_ud;
			break;

		case 's':
			b++;
			if (*b != 'd') {
				printw("invalid format code '%%%ds%c'\n", w, *b);
				return -1;
			}
			if (fields) fields[nfields].fmt = fmt_sd;
			break;

		case 'z':
			b++;
			switch (*b) {
			case 'l': if (fields) fields[nfields].fmt = fmt_lz; break;
			case 't': if (fields) fields[nfields].fmt = fmt_tz; break;
			default:
				printw("invalid format code '%%%dz%c'\n", w, *b);
				return -1;
			}
			break;

		case 'x': if (fields) fields[nfields].fmt = fmt_x; break;
		case 'f': if (fields) fields[nfields].fmt = fmt_f; break;
		case 'e': if (fields) fields[nfields].fmt = fmt_e; break;
		case 'b': if (fields) fields[nfields].fmt = fmt_b; break;
		case 'E': if (fields) fields[nfields].fmt = fmt_E; break;
		case 'o': if (fields) fields[nfields].fmt = fmt_o; break;
		case 'l': if (fields) fields[nfields].fmt = fmt_l; break;
		case 'F': if (fields) fields[nfields].fmt = fmt_F; break;
		case 'P': if (fields) fields[nfields].fmt = fmt_P; break;
		case 'T': if (fields) fields[nfields].fmt = fmt_T; break;
		case 'p': if (fields) fields[nfields].fmt = fmt_p; break;

		case 't':
		case 'C':
		case 'c':

		default:
			printw("invalid format code '%%%d%c'\n", w, *b);
			return -1;
		}
		nfields++;
		b++;

		a = b;
	}

	return nfields;
}

void statusbar(LAYOUT *l)
{
	int i;
	werase(l->status);
	wmove(l->status, 0, 0);
	for (i = 0; i < l->nfields; i++) {
		(*l->fields[i].fmt)(l, l->fields[i].width, &l->fields[i]);
	}
	wnoutrefresh(l->status);
}

FILE *find_config()
{
	FILE *io;
	char *env, path[8192];
	int n;

	env = getenv("VEXRC");
	if (env) {
		io = fopen(env, "r");
		if (io) return io;
	}

	env = getenv("HOME");
	if (env) {
		n = snprintf(path, 8192, "%s/" CONFIG_USERFILE, env);
		if (n < 8192) {
			io = fopen(path, "r");
			if (io) return io;
		}
	}

	return fopen(CONFIG_SYSFILE, "r");
}

CONFIG* configure()
{
	FILE *io;
	CONFIG *c;
	char buf[8192];
	int line;

	c = calloc(1, sizeof(CONFIG));
	io = find_config();
	if (!io) {
		c->layout = strdup("Xa");
		c->status = strdup("vex [%1E] +%o/%l %F ... b[ %64b ]");
		return c;
	}

	line = 0;
	while (fgets(buf, 8192, io) != NULL) {
		char *a, *b;

		line++;

		a = strchr(buf, '\n');
		if (a) *a = '\0';

		for (a = &buf[0];  isspace(*a); a++);
		if (!*a || *a == '#') continue;
		for (b = a;       !isspace(*b); b++);
		*b++ = '\0';

		if (strcmp(a, "layout") == 0) {
			for (a = b; isspace(*a); a++);
			free(c->layout);
			c->layout = strdup(a);
			continue;
		}
		if (strcmp(a, "status") == 0) {
			for (a = b; isspace(*a); a++);
			if (c->status && strlen(c->status) > 0) {
				b = calloc(strlen(c->status) + 1 + strlen(a) + 1, sizeof(char));
				if (!b) {
					printw("memory allocation failed while configuring statusbar.\n");
					anyexit(1);
				}
				sprintf(b, "%s\n%s", c->status, a);
				c->status = b;

			} else {
				c->status = strdup(a);
				if (!c->status) {
					printw("memory allocation failed while configuring statusbar.\n");
					anyexit(1);
				}
			}
			continue;
		}

		printw("Invalid configuration on line %d: '%s'\n", line, buf);
		anyexit(1);
	}

	fclose(io);
	return c;
}

void errorf(LAYOUT * l, const char *msg, ...)
{
	va_list ap;

	wattron(l->command, C_ERROR);
	werase(l->command);
	wmove(l->command, 0, 0);

	va_start(ap, msg);
	vwprintw(l->command, msg, ap);
	va_end(ap);

	wattroff(l->command, C_ERROR);
	wrefresh(l->command);
}

LAYOUT* layout(CONFIG *c, int width)
{
	LAYOUT *l;
	int i, x;

	l = calloc(1, sizeof(LAYOUT));
	if (!l) return NULL;

	l->st_height = 1;
	for (i = 0; i < strlen(c->status); i++) {
		if (c->status[i] == '\n') l->st_height++;
	}

	l->status = newwin(l->st_height, COLS, LINES - l->st_height - 1, 0);
	wattron(l->status, C_STATUS);
	wprintw(l->status, "%*s", COLS, "");

	l->command = newwin(1, COLS, LINES - 1, 0);

	l->nfields = parse_status(c->status, NULL);
	if (l->nfields < 0) return NULL;
	l->fields = calloc(l->nfields, sizeof(FIELD));
	parse_status(c->status, l->fields);

	l->ncol = strlen(c->layout);
	l->main_height = LINES - l->st_height;
	l->width = width;
	l->columns = calloc(l->ncol, sizeof(COLUMN));
	if (!l->columns) return NULL;


	x = 1;
	for (i = 0; i < l->ncol; i++) {
		switch (c->layout[i]) {
		case 'X': x += cfgcol(l, &l->columns[i], pr_hex_pretty, x, 3, 1); break;
		case 'x': x += cfgcol(l, &l->columns[i], pr_hex,        x, 3, 1); break;
		case 'a': x += cfgcol(l, &l->columns[i], pr_ascii,      x, 1, 0); break;
		case 'O': x += cfgcol(l, &l->columns[i], pr_oct_pretty, x, 4, 1); break;
		case 'o': x += cfgcol(l, &l->columns[i], pr_oct,        x, 4, 1); break;
		default:
			printw("bad layout type '%c'\n", c->layout[i]);
			anyexit(1);
			break;
		}
	}

	return l;
}

void draw(LAYOUT *l)
{
	int i, j, max;

	max = l->width * l->main_height;
	if (max > l->len - l->offset) {
		max = l->len - l->offset;
	}

	for (i = 0; i < l->ncol; i++) {
		wclear(l->columns[i].win);
		for (j = 0; j < max; j++) {
			if (j == l->pos) wattron(l->columns[i].win, C_CURSOR);
			(*l->columns[i].pr)(l->columns[i].win, *DATA_AT(l, j));
			if (j == l->pos) wattroff(l->columns[i].win, C_CURSOR);
		}
		wnoutrefresh(l->columns[i].win);
	}

	statusbar(l);
	doupdate();
}

void lpage(LAYOUT *l, int delta)
{
	int page = l->width * l->main_height;

	delta *= page;
	if (delta < 0) {
		if (-1 * delta > l->offset) {
			l->offset = 0;
			draw(l);
			return;
		}
	} else if (l->offset + delta > (l->len + 1) - ((l->len + 1) % page)) {
		l->offset = (l->len + 1) - ((l->len + 1) % page);
		draw(l);
		return;
	}

	l->offset += delta;
	if (l->offset >= l->len) {
		l->offset = l->len - 1;
	}
	draw(l);
}

void lmove(LAYOUT *l, int delta)
{
	int i, x, y, new, max;

	/* FIXME: assuming no need for page shifting */
	new = l->offset + l->pos + delta;
	if (new >= l->len) delta = l->len - (l->offset + l->pos) - 1;
	if (delta == 0 || new < 0) return;

	new = l->pos + delta;
	max = l->main_height * l->width;
	if (new < 0 || new >= max) {
		while (new < 0 && l->offset >= l->width) { /* page up */
			l->offset -= l->width;
			new += l->width;
		}
		while (new >= max) { /* page down */
			l->offset += l->width;
			new -= l->width;
		}
		l->pos = new;
		draw(l);
		return;
	}

	y = l->pos / l->width;

	for (i = 0; i < l->ncol; i++) {
		x = (l->pos - (y * l->width)) * l->columns[i].width;
		wmove(l->columns[i].win, y, x);
		(*l->columns[i].pr)(l->columns[i].win, *DATA_AT(l, l->pos));
	}
	l->pos += delta;
	y = l->pos / l->width;
	for (i = 0; i < l->ncol; i++) {
		x = (l->pos - (y * l->width)) * l->columns[i].width;
		wmove(l->columns[i].win, y, x);
		wattron(l->columns[i].win, C_CURSOR);
		(*l->columns[i].pr)(l->columns[i].win, *DATA_AT(l, l->pos));
		wattroff(l->columns[i].win, C_CURSOR);
		wnoutrefresh(l->columns[i].win);
	}

	statusbar(l);
	doupdate();
}

static uint8_t * mapfile(const char *path, size_t *len)
{
	int fd;
	void *addr;

	if (!len) {
		printw("BUG: mapfile() called with NULL len argument.\n");
		anyexit(1);
	}

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		printw("ERROR: %s\n", strerror(errno));
		anyexit(1);
	}

	*len = lseek(fd, 0, SEEK_END);
	addr = mmap(NULL, *len, PROT_READ, MAP_PRIVATE, fd, 0);
	if (addr == MAP_FAILED) {
		printw("ERROR: %s\n", strerror(errno));
		anyexit(1);
	}

	return addr;
}

int lopen(LAYOUT *l, const char *path)
{
	l->data = mapfile(path, &(l->len));
	if (!l->data) return 0; /* failed */

	l->path = strdup(path);
	l->file = strrchr(l->path, '/');
	if (l->file) l->file++;
	else l->file = l->path;
	return 1;
}

int query(LAYOUT *l, char type, char *buf, size_t len)
{
	WINDOW *win;
	size_t n;
	int c;

	win = newwin(1, COLS, LINES - 1, 0);
	waddch(win, type);

	n = 0;
	for (;;) {
		wrefresh(win);
		c = getch();
		if (c == KEY_ENTER || c == '\n' || c == '\r') {
			/* we can re-use the last query ... */
			if (n > 0) buf[n] = '\0';
			return 0;
		}
		if (isprint(c)) {
			buf[n++] = c;

			if (n == len) {
				errno = ENOBUFS;
				return -1; /* error! */
			}

			waddch(win, c);
			wrefresh(win);
			continue;
		}

		printw("unrec. char code %02x\n", c);
		refresh(); getch();
	}
}

int searchin(uint8_t *haystack, int a, int b, int step, char *needle, size_t len, int *out)
{
	int i, ok;

	/* FIXME: super dumb string search; look at boyer-moore */
	for (; a >= 0 && a != b; a += step) {
		if (haystack[a] != (uint8_t)(needle[0])) continue;

		ok = 1;
		for (i = 1; i < len; i++) {
			if (haystack[a+i] != (uint8_t)(needle[i])) {
				ok = 0;
				break;
			}
		}
		if (ok) {
			*out = a;
			return 0;
		}
	}

	return 1;
}

void search(LAYOUT *l, char *pat)
{
	int rc, offset;
	size_t len = strlen(pat);

	if (len == 0) {
		errorf(l, "No search query provided.");
		return;
	}

	rc = searchin(l->data, l->offset + l->pos + 1, l->len - len, 1, pat, len, &offset);
	if (rc == 0) {
		lmove(l, offset - (l->offset + l->pos));
		return;
	}
	rc = searchin(l->data, 0, min(l->offset + l->pos, l->len - len), 1, pat, len, &offset);
	if (rc == 0) {
		lmove(l, offset - (l->offset + l->pos));
		return;
	}

	errorf(l, "Pattern not found: %s", pat);
}

void rsearch(LAYOUT *l, char *pat)
{
	int rc, offset;
	size_t len = strlen(pat);

	if (len == 0) {
		errorf(l, "No search query provided.");
		return;
	}

	rc = searchin(l->data, l->offset + l->pos - 1, 0, -1, pat, len, &offset);
	if (rc == 0) {
		lmove(l, offset - (l->offset + l->pos));
		return;
	}
	rc = searchin(l->data, l->len - len, l->offset + l->pos, -1, pat, len, &offset);
	if (rc == 0) {
		lmove(l, offset - (l->offset + l->pos));
		return;
	}

	errorf(l, "Pattern not found: %s", pat);
}

int main(int argc, char **argv)
{
	LAYOUT *l;

	if (argc != 2) {
		fprintf(stderr, "USAGE: %s file\n", argv[0]);
		exit(1);
	}

	initscr();
	cbreak();
	keypad(stdscr, TRUE); /* for the arrow keys */
	noecho();
	curs_set(0);
	the_colors();
	refresh();

	l = layout(configure(), 16);
	if (!l) {
		printw("layout() failed...\n");
		anyexit(1);
	}
	if (!lopen(l, argv[1])) {
		printw("lopen() failed...\n");
		anyexit(1);
	}
	draw(l);

	int quant = 0;
	char q[8192] = {0};
	for (;;) {
		int c = getch();
		if (c == 'q') break;

		switch (c) {
		case 'r':
			/* FIXME: leaks memory like a sieve */
			l = layout(configure(), 16);
			if (!l) {
				printw("layout() failed...\n");
				anyexit(1);
			}
			if (!lopen(l, argv[1])) {
				printw("lopen() failed...\n");
				anyexit(1);
			}
			draw(l);
			break;

		case 'n':  search(l, q); break;
		case 'N': rsearch(l, q); break;
		case '/': if (query(l, '/', q, 8192) == 0)  search(l, q); break;
		case '?': if (query(l, '?', q, 8192) == 0) rsearch(l, q); break;

		case KEY_RIGHT: quant = 0; lmove(l,  1); break;
		case KEY_LEFT:  quant = 0; lmove(l, -1); break;
		case KEY_UP:    quant = 0; lmove(l, -1 * l->width); break;
		case KEY_DOWN:  quant = 0; lmove(l,      l->width); break;

		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9': quant = quant * 10 + (c - '0'); break;

		case '+': lmove(l,      (quant ? quant : 1)); quant = 0; break;
		case '-': lmove(l, -1 * (quant ? quant : 1)); quant = 0; break;

		case 'h': lmove(l, -1 * (quant ? quant : 1));            quant = 0; break;
		case 'j': lmove(l, -1 * (quant ? quant : 1) * l->width); quant = 0; break;
		case 'k': lmove(l,      (quant ? quant : 1) * l->width); quant = 0; break;
		case 'l': lmove(l,      (quant ? quant : 1));            quant = 0; break;

		case 'U' & 037:
			if (l->pos < l->width || l->pos >= l->width * (l->main_height - 1)) {
				lpage(l, -1);
			} else {
				lmove(l, -1 * (l->width * l->main_height) / 2);
			}
			break;

		case 'D' & 037:
			if (l->pos < l->width || l->pos >= l->width * (l->main_height - 1)) {
				lpage(l, 1);
			} else {
				lmove(l, (l->width * l->main_height) / 2);
			}
			break;
		}
	}
	endwin();
	return 0;
}
