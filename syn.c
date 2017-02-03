#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctype.h>

#define FG_WHITE     0x00
#define FG_RED       0x10
#define FG_GREEN     0x20
#define FG_YELLOW    0x30
#define FG_BLUE      0x40
#define FG_MAGENTA   0x50
#define FG_CYAN      0x60
#define FG_BLACK     0x70

#define BG_BLACK     0x00
#define BG_RED       0x01
#define BG_GREEN     0x02
#define BG_YELLOW    0x03
#define BG_BLUE      0x04
#define BG_MAGENTA   0x05
#define BG_CYAN      0x06
#define BG_WHITE     0x07

#define INVALID_COLOR 0xff

#define T_EOF    0
#define T_ERROR  1
#define T_OCTET  256
#define T_STRING 257
#define T_OFFSET 258
#define T_OFFREF 259
#define T_COLOR  260
#define T_OPEN   '{'
#define T_CLOSE  '}'
#define T_TERMIN ';'

typedef struct __field FIELD;
struct __field {
	uint16_t width;
	uint8_t  color;
	FIELD   *next;
};

typedef struct __match MATCH;
struct __match {
	size_t   patlen;
	uint8_t *pattern;
	uint8_t  color;

	FIELD   *fields;
	MATCH   *subs;

	MATCH   *sibling;
	MATCH   *thread;
};

#define TOKMAX 8191
typedef struct {
	int   fd;
	off_t len;
	char *src;
	off_t i;

	int line;
	int position;

	int last;
	char token[TOKMAX + 1];
} LEXER;

#define LEOF(l) ((l)->i >= (l)->len)
#define LCHR(l) ((l)->src[(l)->i])
#define LREW(l,t) ((l)->last = (t))
static inline void LRST(LEXER *l) {
	l->last = 0;
	l->i = 0;
	l->token[0] = '\0';
	l->line = 1;
	l->position = 0;
}
static inline void LADV(LEXER *l) {
	l->position++;
	if (LCHR(l) == '\n') {
		l->line++;
		l->position = 0;
	}
	l->i++;
}
static inline void LERR(LEXER *l, const char *msg, ...)
{
	va_list ap;
	int i;
	char *line;

	va_start(ap, msg);
	fprintf(stderr, "%d:%d: ", l->line, l->position);
	vfprintf(stderr, msg, ap);

	for (i = l->position; l->src[l->i - l->position + i] != '\n'; i++);
	line = strndup(l->src + l->i - l->position, i);
	if (line) {
		fprintf(stderr, "%s\n", line);
		fprintf(stderr, "%*s^^^^^\n", (int)(l->position - strlen(l->token)), "");
		free(line);
	}

	/* FIXME: print offending line */
	va_end(ap);
	exit(3);
}


int lex_open(LEXER *l, const char *path)
{
	l->fd = open(path, O_RDONLY);
	if (l->fd < 0) {
		return -1;
	}

	l->last = 0;
	l->i = 0;
	l->len = lseek(l->fd, 0, SEEK_END);
	l->src = mmap(NULL, l->len, PROT_READ, MAP_PRIVATE, l->fd, l->i);
	if (l->src == MAP_FAILED) {
		return -1;
	}

	l->line = 1;
	l->position = 0;
	return 0;
}

int lex(LEXER *l)
{
	int i, token;

	if (l->last) {
		token = l->last;
		l->last = 0;
		return token;
	}

again:
	while (!LEOF(l) && isspace(LCHR(l))) {
		LADV(l);
	}
	if (l->i == l->len) {
		return T_EOF;
	}

	l->token[0] = '\0';
	switch (LCHR(l)) {
	case '#':
		while (l->i < l->len && LCHR(l) != '\n') {
			LADV(l);
		}
		goto again;

	case '"':
		i = 0; LADV(l);
		while (i < TOKMAX && l->i < l->len && LCHR(l) != '"') {
			l->token[i++] = LCHR(l);
			LADV(l);
		}
		if (LEOF(l)) {
			return T_ERROR;
		}
		if (LCHR(l) != '"') {
			return T_ERROR;
		}
		if (i == TOKMAX) {
			return T_ERROR;
		}
		LADV(l);
		l->token[i] = '\0';
		return T_STRING;

	case '+':
		i = 0; LADV(l);
		while (i < TOKMAX && !LEOF(l) && isdigit(LCHR(l))) {
			l->token[i++] = LCHR(l);
			LADV(l);
		}
		if (i == 0) {
			return T_ERROR;
		}
		if (i == TOKMAX) {
			return T_ERROR;
		}
		l->token[i] = '\0';
		return T_OFFSET;

	case '&':
		i = 0; LADV(l);
		while (i < TOKMAX && !LEOF(l) && isdigit(LCHR(l))) {
			l->token[i++] = LCHR(l);
			LADV(l);
		}
		if (i == 0) {
			return T_ERROR;
		}
		if (i == TOKMAX) {
			return T_ERROR;
		}
		l->token[i] = '\0';
		return T_OFFREF;

	case '{': LADV(l); return T_OPEN;
	case '}': LADV(l); return T_CLOSE;
	case ';': LADV(l); return T_TERMIN;

	default:
		/* that leaves T_COLOR and T_OCTET */
		token = T_OCTET;
		i = 0;
		while (i < TOKMAX && !LEOF(l)) {
			if (isxdigit(LCHR(l))) {
				l->token[i++] = LCHR(l); LADV(l);
			} else if (isalnum(LCHR(l)) || LCHR(l) == '/') {
				token = T_COLOR;
				l->token[i++] = LCHR(l); LADV(l);
			} else {
				break;
			}
		}
		if (i == 0) {
			return T_ERROR;
		}
		if (i == TOKMAX) {
			return T_ERROR;
		}

		l->token[i++] = '\0';
		return token;
	}

	return T_ERROR;
}

int valueof(const char *s, int max)
{
	int v = 0;
	int i = 0;

	for (i = 0; s[i]; i++) {
		v = v * 10 + s[i] - '0';
		if (v > max) {
			fprintf(stderr, "maximum value of %d exceeded.\n", max);
			exit(3);
		}
	}
	return v;
}

int colorfor(const char *s)
{
	char *c;
	int fg, bg;

	c = strchr(s, '/');
	if (c) {
		if (strrchr(s, '/') != c) return INVALID_COLOR;
		*c++ = '\0';
		fg = colorfor(s);
		if (fg == INVALID_COLOR) return INVALID_COLOR;
		bg = colorfor(c);;
		if (bg == INVALID_COLOR) return INVALID_COLOR;
		return (fg & 0xf0) | (bg >> 4);
	}

	if (strcmp(s, "white")   == 0) return FG_WHITE   | BG_BLACK;
	if (strcmp(s, "red")     == 0) return FG_RED     | BG_BLACK;
	if (strcmp(s, "green")   == 0) return FG_GREEN   | BG_BLACK;
	if (strcmp(s, "yellow")  == 0) return FG_YELLOW  | BG_BLACK;
	if (strcmp(s, "blue")    == 0) return FG_BLUE    | BG_BLACK;
	if (strcmp(s, "magenta") == 0) return FG_MAGENTA | BG_BLACK;
	if (strcmp(s, "cyan")    == 0) return FG_CYAN    | BG_BLACK;
	if (strcmp(s, "black")   == 0) return FG_BLACK   | BG_WHITE;
	return INVALID_COLOR;
}

const char * tokname(int token)
{
	switch (token) {
	case T_EOF:    return "EOF";
	case T_ERROR:  return "ERROR";
	case T_OCTET:  return "OCTET";
	case T_STRING: return "STRING";
	case T_OFFSET: return "OFFSET";
	case T_OFFREF: return "OFFREF";
	case T_COLOR:  return "COLOR";
	case T_OPEN:   return "TERMINATOR ({)";
	case T_CLOSE:  return "TERMINATOR (})";
	case T_TERMIN: return "TERMINATOR (;)";
	default:       return "*unknoen*";
	}
}

#if 0
/* allocate best usage of memory */
void preparse1(LEXER *l, size_t *nmatches, size_t *nfields, size_t *nbytes)
{
	int token;

	while ((token = lex(l)) != T_EOF) {
		if (token != T_OCTET && token != T_STRING) {
			LERR(l, "unexpected %s token (expecting either hex string or a character string).\n", tokname(token));
		}

		*nmatches += 1;

		do {
			*nbytes += strlen(l->token);
			token = lex(l);
		} while (token == T_OCTET || token == T_STRING);

		switch (token) {
		case T_EOF: LERR(l, "unexpected EOF.\n");
		case T_COLOR:
			token = lex(l);
			if (token != T_OPEN) LERR(l, "unexpected %s token (expecting an opening curly brace, '{').\n", tokname(token));
		case T_OPEN: goto match_fields;
		default: LERR(l, "unexpected %s token (expecting an opening curly brace, '{', or a color specification).\n", tokname(token));
		}

match_fields:
		token = lex(l);
		switch (token) {
		case T_EOF: LERR(l, "unexpected EOF.\n");

		case T_OFFSET:
		case T_OFFREF:
			*nfields += 1;
			token = lex(l);
			switch (token) {
			case T_EOF: LERR(l, "unexpected EOF.\n");
			case T_TERMIN: goto match_fields;
			case T_COLOR:
				token = lex(l);
				if (token == T_TERMIN) goto match_fields;
				LERR(l, "unexpected %s token (expecting a statement terminator, ';').\n", tokname(token));

			default: LERR(l, "unexpected %s token (expecting either a color specification, or a statement terminator, ';')\n", tokname(token));
			}
			break;

		case T_STRING:
		case T_OCTET: goto match_submatches;

		case T_CLOSE: return;
		default: LERR(l, "unexpected %s token.", tokname(token));
		}

match_submatches:
		*nmatches += 1;
		LREW(l, token);
		preparse1(l, nmatches, nfields, nbytes);
	}
}

typedef struct {
	size_t num_matches;
	size_t next_match;
	MATCH *matches;

	size_t num_fields;
	size_t next_field;
	FIELD *fields;

	size_t num_bytes;
	size_t next_byte;
	uint8_t *bytes;
} MEMBLOCK;
MEMBLOCK* prepare(LEXER *l)
{
	MEMBLOCK *mb = calloc(1, sizeof(MEMBLOCK));
	if (!mb) return NULL;

	preparse1(l, &mb->num_matches, &mb->num_fields, &mb->num_bytes);
	mb->matches = calloc(mb->num_matches, sizeof(MATCH));
	mb->fields  = calloc(mb->num_fields,  sizeof(FIELD));
	mb->bytes   = calloc(mb->num_bytes,   sizeof(uint8_t));
	return mb;
}

MATCH *alloc_match()
{
	return NULL;
}

FIELD *alloc_field()
{
	return NULL;
}

uint8_t *alloc_pattern(size_t n)
{
	return NULL;
}
#endif

#define MAXBUF 65536
MATCH *parse(LEXER *l)
{
	int token, i, v;
	MATCH *m, *m2;
	FIELD *f;
	char buf[MAXBUF];

	while ((token = lex(l)) != T_EOF) {
		if (token != T_OCTET && token != T_STRING) {
			LERR(l, "unexpected %s token (expecting either hex string or a character string).\n", tokname(token));
		}

		m = calloc(1, sizeof(MATCH));
		if (!m) {
			fprintf(stderr, "failed to allocate memory: %s (error %d)\n", strerror(errno), errno);
			exit(2);
		}

		i = 0;
		do {
			memcpy(buf + i, l->token, strlen(l->token));
			i += strlen(l->token); /* FIXME: maybe track token length? */
			token = lex(l);
		} while (token == T_OCTET || token == T_STRING);
		m->patlen = i;
		m->pattern = (uint8_t *)strndup(buf, i);

		switch (token) {
		case T_EOF: LERR(l, "unexpected EOF.\n");
		case T_COLOR:
			m->color = colorfor(l->token);
			token = lex(l);
			if (token != T_OPEN) LERR(l, "unexpected %s token (expecting an opening curly brace, '{').\n", tokname(token));
		case T_OPEN: i = 1; goto match_fields;
		default: LERR(l, "unexpected %s token (expecting an opening curly brace, '{', or a color specification).\n", tokname(token));
		}

match_fields:
		token = lex(l);
		switch (token) {
		case T_EOF: LERR(l, "unexpected EOF.\n");
		case T_OFFSET:
			v = valueof(l->token, 65536);
			if (i == 1) {
				f = m->fields = calloc(1, sizeof(FIELD));
				if (!m->fields) {
					fprintf(stderr, "failed to allocate memory: %s (error %d)\n", strerror(errno), errno);
					exit(2);
				}

			} else {
				for (f = m->fields; f->next; f = f->next);
				f->next = calloc(1, sizeof(FIELD));
				if (!f->next) {
					fprintf(stderr, "failed to allocate memory: %s (error %d)\n", strerror(errno), errno);
					exit(2);
				}
				f = f->next;
			}
			i++;

			token = lex(l);
			switch (token) {
			case T_EOF: LERR(l, "unexpected EOF.\n");
			case T_TERMIN: goto match_fields;
			case T_COLOR:
				f->color = colorfor(l->token);
				token = lex(l);
				if (token == T_TERMIN) goto match_fields;
				LERR(l, "unexpected %s token (expecting a statement terminator, ';').\n", tokname(token));

			default: LERR(l, "unexpected %s token (expecting either a color specification, or a statement terminator, ';')\n", tokname(token));
			}
			break;

		case T_OFFREF:
			v = valueof(l->token, 256);
			if (v < 1) {
				fprintf(stderr, "invalid reference ('%s'); indexing starts at 1.\n", l->token);
				exit(3);
			}
			if (v == i) {
				fprintf(stderr, "invalid reference ('%s'); refers to itself.\n", l->token);
				exit(3);
			}
			if (v > i) {
				fprintf(stderr, "invalid reference ('%s'); refers to a later field.\n", l->token);
				fprintf(stderr, "v=%d; i=%d\n", v, i);
				exit(3);
			}
			for (f = m->fields; f->next; f = f->next);
			if (f->width & 0x8000) {
				fprintf(stderr, "invalid reference ('%s'); refers to another reference.\n", l->token);
				exit(3);
			}

			f->next = calloc(1, sizeof(FIELD));
			if (!f->next) {
				fprintf(stderr, "failed to allocate memory: %s (error %d)\n", strerror(errno), errno);
				exit(2);
			}
			f->next->width = v | 0x8000;
			i++;

			token = lex(l);
			switch (token) {
			case T_EOF: LERR(l, "unexpected EOF.\n");
			case T_TERMIN:
				f->next->color = FG_WHITE|BG_BLACK;
				goto match_fields;

			case T_COLOR:
				f->next->color = colorfor(l->token);
				token = lex(l);
				if (token == T_TERMIN) goto match_fields;
				LERR(l, "unexpected %s token (expecting a statement terminator, ';').\n", tokname(token));

			default: LERR(l, "unexpected %s token (expecting either a color specification, or a statement terminator, ';').n", tokname(token));
			}
			break;

		case T_STRING:
		case T_OCTET: goto match_submatches;


		case T_CLOSE: goto done;
		default: LERR(l, "unexpected %s token.n", tokname(token));
		}

match_submatches:
		LREW(l, token);
		if (!m->subs) {
			m->subs = parse(l);
		} else {
			for (m2 = m->subs; m2->sibling; m2 = m2->sibling);
			m2->sibling = parse(l);
		}
	}

done:
	return m;
}

void mustwrite(int fd, void *buf, size_t n)
{
	ssize_t nwrit;
	nwrit = write(fd, buf, n);
	if (nwrit != n) {
		perror("write");
		exit(1);
	}
}

void summarize(MATCH *m, uint32_t *nmatches, uint32_t *nfields, uint32_t *nbytes)
{
	FIELD *f;
	MATCH *last, *m2;

	last = m;
	*nbytes += m->patlen;
	//m->index = *nmatches++;
	for (f = m->fields; f; f = f->next) {
		*nfields += 1;
	}
	for (m2 = m->subs; m2; m2 = m2->sibling) {
		//m2->parent = m;
		last->thread = m2;
		last = m2;
		summarize(m2, nmatches, nfields, nbytes);
	}
	for (m2 = m->sibling; m2; m2 = m2->sibling) {
		last->thread = m2;
		last = m2;
		summarize(m2, nmatches, nfields, nbytes);
	}
}

void compile_match(int fd, MATCH *m)
{
	uint16_t len;
	uint32_t n;
	FIELD *f;
	MATCH *m2;

	//summarize(m);

	for (n = 0, f = m->fields; f; n++, f = f->next);
	mustwrite(fd, &n, 4);
	for (n = 0, m2 = m->subs; m2; n++, m2 = m2->sibling);
	mustwrite(fd, &n, 4);

	len = m->patlen;
	mustwrite(fd, &len, 2);
	mustwrite(fd, m->pattern, m->patlen);
	mustwrite(fd, &m->color, 1);

	for (f = m->fields; f; f = f->next) {
		mustwrite(fd, &f->width, 2);
		mustwrite(fd, &f->color, 1);
	}
	for (m = m->subs; m != NULL; m = m->sibling) {
		compile_match(fd, m);
	}
}

void compile(int fd, MATCH *m)
{
	uint32_t nmatches, nfields, nbytes;
	MATCH *last, *m1;

	nmatches = nfields = 0;
	for (m1 = m; m1; m1 = m1->sibling) {
		summarize(m1, &nmatches, &nfields, &nbytes);
	}
	mustwrite(fd, "\x17VSB\1", 5);
	mustwrite(fd, &nmatches, 4);
	for (m1 = m; m1; m1 = m1->sibling) {
		summarize(m1, &nmatches, &nfields, &nbytes);
	}
	mustwrite(fd, &nfields, 4);
	for (m1 = m; m1; m1 = m1->sibling) {
		summarize(m1, &nmatches, &nfields, &nbytes);
	}
	mustwrite(fd, &nbytes, 4);
	for (m1 = m; m1; m1 = m1->sibling) {
		summarize(m1, &nmatches, &nfields, &nbytes);
	}


	compile_match(fd, m);
}

int main(int argc, char **argv)
{
	LEXER l;
	int fd;

	if (argc != 3) {
		fprintf(stderr, "USAGE: %s path/to/syntax.src path/to/output.bin\n", argv[0]);
		return 1;
	}

	if (lex_open(&l, argv[1]) != 0) {
		fprintf(stderr, "%s: %s (error %d)\n", argv[1], strerror(errno), errno);
		return 1;
	}

	fd = open(argv[2], O_WRONLY|O_CREAT|O_TRUNC, 0666);
	if (fd < 0) {
		fprintf(stderr, "%s: %s (error %d)\n", argv[2], strerror(errno), errno);
		return 1;
	}

	compile(fd, parse(&l));
	return 0;
}
