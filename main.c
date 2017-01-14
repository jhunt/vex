#include <stdio.h>
#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <sys/mman.h>

/* formatting constants {{{ */
#define GUTTER 5
/* }}} */
/* TYPES {{{ */
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

static char hexdig(int v)
{
	if (v < 10) return v + '0';
	return v - 10 + 'a';
}

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

static void the_colors()
{
	start_color();
	init_pair(C_NORMAL_IDX, COLOR_WHITE, COLOR_BLACK);
	init_pair(C_CURSOR_IDX, COLOR_BLACK, COLOR_WHITE);
	init_pair(C_STATUS_IDX, COLOR_GREEN, COLOR_BLACK);
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

static void fmt_literal(void *l, int width, void *_field)
{
	wprintw(((LAYOUT *)l)->status, "%s", ((FIELD*)_field)->literal);
}

static void fmt_ud(void *_, int width, void *_field)
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
}

static void fmt_sd(void *_, int width, void *_field)
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
}

static void fmt_b(void *_, int width, void *_field)
{
	int left, i;
	uint64_t v, mask;
	LAYOUT *l;

	l = (LAYOUT *)_;
	left = l->len - (l->offset + l->pos);

	mask = 0x80;
	switch (width) {
	case 8:  v = as_u8( DATA_AT(l, l->pos));              break;
	case 16: v = as_u16(DATA_AT(l, l->pos)); mask <<= 8;  break;
	case 32: v = as_u32(DATA_AT(l, l->pos)); mask <<= 24; break;
	case 64: v = as_u64(DATA_AT(l, l->pos)); mask <<= 56; break;
	default:
		wprintw(l->status, "!!!");
		return;
	}

	for (i = 0; i < width; i++) {
		if (i && i % 4 == 0) waddch(l->status, ' ');
		waddch(l->status, (v & mask != 0) ? '1' : '0');
		v >>= 1;
	}
}

static void fmt_E(void *_, int width, void *_fields)
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
}

static void fmt_o(void *_, int width, void *_field)
{
	LAYOUT *l;

	l = (LAYOUT *)_;
	wprintw(l->status, "%ld", l->offset + l->pos);
}

static void fmt_F(void *_, int width, void *_field)
{
	LAYOUT *l;

	l = (LAYOUT *)_;
	wprintw(l->status, "%s", l->file);
}

static void fmt_P(void *_, int width, void *_field)
{
	LAYOUT *l;

	l = (LAYOUT *)_;
	wprintw(l->status, "%s", l->path);
}

static void fmt_x(void *_, int width, void *_field)
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
}

static void fmt_f(void *_, int width, void *_field)
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
}

static void fmt_e(void *_, int width, void *_field)
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
}

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

		case 'x': if (fields) fields[nfields].fmt = fmt_x; break;
		case 'f': if (fields) fields[nfields].fmt = fmt_f; break;
		case 'e': if (fields) fields[nfields].fmt = fmt_e; break;
		case 'b': if (fields) fields[nfields].fmt = fmt_b; break;
		case 'E': if (fields) fields[nfields].fmt = fmt_E; break;
		case 'o': if (fields) fields[nfields].fmt = fmt_o; break;
		case 'F': if (fields) fields[nfields].fmt = fmt_F; break;
		case 'P': if (fields) fields[nfields].fmt = fmt_P; break;

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
	wclear(l->status);
	for (i = 0; i < l->nfields; i++) {
		(*l->fields[i].fmt)(l, l->fields[i].width, &l->fields[i]);
	}
	wnoutrefresh(l->status);
}

LAYOUT* layout(const char *str, const char *status, int width)
{
	LAYOUT *l;
	int i, x;

	l = calloc(1, sizeof(LAYOUT));
	if (!l) return NULL;

	l->st_height = 2;
	l->status = newwin(l->st_height, COLS, LINES - l->st_height, 0);
	wattron(l->status, C_STATUS);
	wprintw(l->status, "%*s", COLS, "");

	l->nfields = parse_status(status, NULL);
	if (l->nfields < 0) return NULL;
	l->fields = calloc(l->nfields, sizeof(FIELD));
	parse_status(status, l->fields);

	l->ncol = strlen(str);
	l->main_height = LINES - l->st_height;
	l->width = width;
	l->columns = calloc(l->ncol, sizeof(COLUMN));
	if (!l->columns) return NULL;


	x = 1;
	for (i = 0; i < l->ncol; i++) {
		switch (str[i]) {
		case 'X': x += cfgcol(l, &l->columns[i], pr_hex_pretty, x, 3, 1); break;
		case 'x': x += cfgcol(l, &l->columns[i], pr_hex,        x, 3, 1); break;
		case 'a': x += cfgcol(l, &l->columns[i], pr_ascii,      x, 1, 0); break;
		case 'O': x += cfgcol(l, &l->columns[i], pr_oct_pretty, x, 4, 1); break;
		case 'o': x += cfgcol(l, &l->columns[i], pr_oct,        x, 4, 1); break;
		default:
			printw("bad layout type '%c'\n", str[i]);
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

void lmove(LAYOUT *l, int delta)
{
	int i, x, y, newY;

	/* FIXME: assuming no need for page shifting */
	y = l->pos / l->width;
	if (l->pos + delta < 0) {
		return;
	}

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

	l = layout("XxOa", "w(%16ud / %16sd) d(%32ud / %32sd) q(%64ud / %64sd) f(%32e / %64e)\n%F (%P)", 16);
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
	for (;;) {
		int c = getch();
		if (c == 'q') break;

		switch (c) {
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
		case '+': lmove(l,      quant); quant = 0; break;
		case '-': lmove(l, -1 * quant); quant = 0; break;
		}
	}
	endwin();
	return 0;
}
