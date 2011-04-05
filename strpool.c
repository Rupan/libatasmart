/*-*- Mode: C; c-basic-offset: 8 -*-*/

/***
    This file is part of libatasmart.

    Copyright 2008 Lennart Poettering

    libatasmart is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as
    published by the Free Software Foundation, either version 2.1 of the
    License, or (at your option) any later version.

    libatasmart is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with libatasmart. If not, If not, see
    <http://www.gnu.org/licenses/>.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <assert.h>
#include <errno.h>

typedef struct item {
        char *cnt;
        size_t cntl;
        char *text;
        size_t size;
        unsigned idx;
        struct item *suffix_of;
        struct item *next;
} item;

static void free_items(struct item *first) {

        while (first) {
                struct item *n = first->next;

                free(first->cnt);
                free(first->text);
                free(first);

                first = n;
        }
}

static void find_suffixes(struct item *first) {
        struct item *i, *j;

        for (i = first; i; i = i->next) {
                int right = 0;

                for (j = first; j; j = j->next) {

                        if (i == j) {
                                right = 1;
                                continue;
                        }

                        if (i->size > j->size)
                                continue;

                        if (i->size == j->size && !right)
                                continue;

                        if (memcmp(i->text, j->text+j->size-i->size, i->size) != 0)
                                continue;

                        i->suffix_of = j;
                        break;
                }
        }
}

static void fill_idx(struct item *first) {
        struct item *i;
        unsigned k = 0;

        for (i = first; i; i = i->next) {
                if (i->suffix_of)
                        continue;

                i->idx = k;
                k += i->size+1;
        }

        for (i = first; i; i = i->next) {
                struct item *p;

                if (!i->suffix_of)
                        continue;

                for (p = i->suffix_of; p->suffix_of; p = p->suffix_of)
                        ;

                assert(i->size <= p->size);
                assert(memcmp(i->text, p->text + p->size - i->size, i->size) == 0);

                i->idx = p->idx + p->size - i->size;
        }
}

static void dump_string(FILE *out, struct item *i) {
        const char *t;

        fputs("\n\t\"", out);

        for (t = i->text; t < i->text+i->size; t++) {
                switch (*t) {
                        case '\\':
                                fputs("\\\\", out);
                                break;
                        case '\"':
                                fputs("\\\"", out);
                                break;
                        case '\'':
                                fputs("\\'", out);
                                break;
                        case '\n':
                                fputs("\\n\"\n\t\"", out);
                                break;
                        case '\r':
                                fputs("\\r", out);
                                break;
                        case '\b':
                                fputs("\\b", out);
                                break;
                        case '\t':
                                fputs("\\t", out);
                                break;
                        case '\f':
                                fputs("\\f", out);
                                break;
                        case '\a':
                                fputs("\\f", out);
                                break;
                        case '\v':
                                fputs("\\v", out);
                                break;
                        default:
                                if (*t >= 32 && *t < 127)
                                        putc(*t, out);
                                else
                                        fprintf(out, "\\x%02x", *t);
                                break;
                }
        }

        fputs("\\0\"", out);
}

static void dump_text(FILE *out, struct item *first) {
        struct item *i;

        for (i = first; i; i = i->next) {

                if (i->cnt)
                        fwrite(i->cnt, 1, i->cntl, out);

                /* We offset all indexes by one, to avoid clashes
                 * between index 0 and NULL */
                fprintf(out, "((const char*) %u)", i->idx+1);
        }
}

static void dump_pool(FILE *out, struct item *first) {
        struct item *i;
        int saved_rel=-1, saved_bytes=0, saved_strings=0;

        for (i = first; i; i = i->next) {
                saved_rel++;

                if (i->suffix_of) {
                        saved_strings ++;
                        saved_bytes += i->size;
                }
        }

        fprintf(out, "/* Saved %i relocations, saved %i strings (%i b) due to suffix compression. */\n", saved_rel, saved_strings, saved_bytes);

        fputs("static const char _strpool_[] =", out);

        for (i = first; i; i = i->next) {

                if (i->suffix_of)
                        fputs("\n\t/*** Suppressed due to suffix: ", out);

                dump_string(out, i);

                if (i->suffix_of)
                        fputs(" ***/", out);

        }

        fputs(";\n", out);
}

static char *append(char *r, size_t *rl, char **c, size_t n) {

        r = realloc(r, *rl + n);

        if (!r)
                abort();

        memcpy(r + *rl, *c, n);

        *rl += n;
        *c += n;

        return r;
}

static int parse_hex_digit(char c) {

        if (c >= '0' && c <= '9')
                return c - '0' + 0x0;

        if (c >= 'a' && c <= 'f')
                return c - 'a' + 0xA;

        if (c >= 'A' && c <= 'F')
                return c - 'A' + 0xA;

        return -1;
}

static int parse_hex(const char *t, char *r) {
        int a, b = 0;
        int k = 1;

        if ((a = parse_hex_digit(t[0])) < 0)
                return -1;

        if (t[1]) {
                if ((b = parse_hex_digit(t[1])) < 0)
                        b = 0;
                else
                        k = 2;
        }

        *r = (a << 4) | b;
        return k;
}

static int parse_oct_digit(char c) {

        if (c >= '0' && c <= '7')
                return c - '0';

        return -1;
}

static int parse_oct(const char *t, char *r) {
        int a, b = 0, c = 0, m;
        int k = 1;

        if ((a = parse_oct_digit(t[0])) < 0)
                return -1;

        if (t[1]) {

                if ((b = parse_oct_digit(t[1])) < 0)
                        b = 0;
                else {
                        k = 2;

                        if (t[2]) {

                                if ((c = parse_oct_digit(t[2])) < 0)
                                        c = 0;
                                else
                                        k = 3;
                        }
                }
        }

        m = (a << 6) | (b << 3) | c;

        if (m > 0xFF)
                return -1;

        *r = (char) m;

        return k;
}

static int parse(FILE *in, const char *fname, struct item **rfirst, char **remain, size_t *remain_size) {

        int enabled = 0;
        enum {
                STATE_TEXT,
                STATE_COMMENT_C,
                STATE_COMMENT_CPP,
                STATE_STRING,
                STATE_CHAR,
        } state = STATE_TEXT;

        char *r = NULL;
        size_t rl = 0;
        char *cnt = NULL;
        size_t cntl = 0;
        struct item *first = NULL, *last = NULL;
        unsigned nline = 0;
        unsigned pool_started_line = 0;
        *rfirst = NULL;

        if (!fname)
                fname = "<stdin>";

        for (;;) {
                char t[1024], *c;
                int done = 0;

                if (!(fgets(t, sizeof(t), in))) {

                        if (feof(in))
                                break;

                        fprintf(stderr, "Failed to read: %s\n", strerror(errno));
                        goto fail;
                }

                nline++;

                c = t;

                do {

/*             fprintf(stderr, "enabled %i, state %i, cnt %i, remaining string is: %s", enabled, state, !!cnt, c); */

                        switch (state) {

                                case STATE_TEXT:

                                        if (!strncmp(c, "/*", 2)) {
                                                state = STATE_COMMENT_C;
                                                r = append(r, &rl, &c, 2);
                                        } else if (!strncmp(c, "//", 2)) {
                                                state = STATE_COMMENT_CPP;
                                                r = append(r, &rl, &c, 2);
                                        } else if (*c == '"') {
                                                state = STATE_STRING;

                                                if (enabled) {
                                                        cnt = r;
                                                        cntl = rl;

                                                        r = NULL;
                                                        rl = 0;

                                                        c ++;
                                                } else
                                                        r = append(r, &rl, &c, 1);
                                        } else if (*c == '\'') {
                                                state = STATE_CHAR;
                                                r = append(r, &rl, &c, 1);
                                        } else if (*c == 0)
                                                done = 1;
                                        else
                                                r = append(r, &rl, &c, 1);

                                        break;

                                case STATE_COMMENT_C:

                                        if (!strncmp(c, "*/", 2)) {
                                                state = STATE_TEXT;
                                                r = append(r, &rl, &c, 2);
                                        } else if (!strncmp(c, "%STRINGPOOLSTART%", 17)) {
                                                enabled = 1;
                                                pool_started_line = nline;
                                                r = append(r, &rl, &c, 17);
                                        } else if (!strncmp(c, "%STRINGPOOLSTOP%", 16)) {
                                                enabled = 0;
                                                r = append(r, &rl, &c, 16);
                                        } else if (*c == 0)
                                                done = 1;
                                        else
                                                r = append(r, &rl, &c, 1);

                                        break;

                                case STATE_COMMENT_CPP:

                                        if (*c == '\n' || *c == '\r') {
                                                state = STATE_TEXT;
                                                r = append(r, &rl, &c, 1);
                                        } else if (!strncmp(c, "%STRINGPOOLSTART%", 17)) {
                                                enabled = 1;
                                                pool_started_line = nline;
                                                r = append(r, &rl, &c, 17);
                                        } else if (!strncmp(c, "%STRINGPOOLSTOP%", 16)) {
                                                enabled = 0;
                                                r = append(r, &rl, &c, 16);
                                        } else if (*c == 0) {
                                                state = STATE_TEXT;
                                                done = 1;
                                        } else
                                                r = append(r, &rl, &c, 1);

                                        break;

                                case STATE_STRING:
                                case STATE_CHAR:

                                        if ((*c == '\'' && state == STATE_CHAR) || (*c == '"' && state == STATE_STRING)) {

                                                if (state == STATE_STRING && enabled) {
                                                        struct item *i;
                                                        i = malloc(sizeof(struct item));

                                                        if (!i)
                                                                abort();

                                                        i->cnt = cnt;
                                                        i->cntl = cntl;

                                                        cnt = NULL;
                                                        cntl = 0;

                                                        i->text = r;
                                                        i->size = rl;

                                                        r = NULL;
                                                        rl = 0;

                                                        i->next = NULL;
                                                        i->suffix_of = NULL;

                                                        if (last)
                                                                last->next = i;
                                                        else
                                                                first = i;

                                                        last = i;

                                                        c++;

                                                } else
                                                        r = append(r, &rl, &c, 1);

                                                state = STATE_TEXT;

                                        } else if (*c == '\\') {

                                                char d;
                                                char l = 2;

                                                switch (c[1]) {

                                                        case '\\':
                                                        case '"':
                                                        case '\'':
                                                        case '?':
                                                                d = c[1];
                                                                break;
                                                        case 'n':
                                                                d = '\n';
                                                                break;
                                                        case 'r':
                                                                d = '\r';
                                                                break;
                                                        case 'b':
                                                                d = '\b';
                                                                break;
                                                        case 't':
                                                                d = '\t';
                                                                break;
                                                        case 'f':
                                                                d = '\f';
                                                                break;
                                                        case 'a':
                                                                d = '\a';
                                                                break;
                                                        case 'v':
                                                                d = '\v';
                                                                break;
                                                        case 'x': {
                                                                int k;
                                                                if ((k = parse_hex(c+2, &d)) < 0) {
                                                                        fprintf(stderr, "%s:%u: Parse failure: invalid hexadecimal escape sequence.\n", fname, nline);
                                                                        goto fail;
                                                                }
                                                                l = 2 + k;
                                                                break;
                                                        }
                                                        case '0':
                                                        case '1':
                                                        case '2':
                                                        case '3':
                                                        case '4':
                                                        case '5':
                                                        case '6':
                                                        case '7': {
                                                                int k;
                                                                if ((k = parse_oct(c+1, &d)) < 0) {
                                                                        fprintf(stderr, "%s:%u: Parse failure: invalid octal escape sequence.\n", fname, nline);
                                                                        goto fail;
                                                                }
                                                                l = 1 + k;
                                                                break;
                                                        }
                                                        default:
                                                                fprintf(stderr, "%s:%u: Parse failure: invalid escape sequence.\n", fname, nline);
                                                                goto fail;
                                                }

                                                if (state == STATE_STRING && enabled) {
                                                        char *x = &d;
                                                        r = append(r, &rl, &x, 1);
                                                        c += l;
                                                } else
                                                        r = append(r, &rl, &c, l);
                                        } else if (*c == 0) {
                                                fprintf(stderr, "%s:%u: Parse failure: multiline strings suck.\n", fname, nline);
                                                goto fail;
                                        } else
                                                r = append(r, &rl, &c, 1);

                                        break;
                        }
                } while (!done);
        }

        if (enabled) {
                fprintf(stderr, "%s:%u: Parse failure: missing %%STRINGPOOLSTOP%%\n", fname, pool_started_line);
                goto fail;
        }

        if (state != STATE_TEXT) {
                fprintf(stderr, "%s:%u: Parse failure: unexpected EOF.\n", fname, nline);
                goto fail;
        }

        assert(!cnt);

        *rfirst = first;

        *remain = r;
        *remain_size = rl;

        return 0;

fail:

        free(cnt);
        free(r);
        free_items(first);

        return -1;
}

static int process(FILE *in, FILE *out, const char*ifname) {

        struct item *first = NULL;
        char *remain = NULL;
        size_t remain_size = 0;

        if (parse(in, ifname, &first, &remain, &remain_size) < 0)
                return -1;

        if (!first)
                fwrite(remain, 1, remain_size, out);
        else {
                find_suffixes(first);
                fill_idx(first);

                dump_pool(out, first);

                fprintf(out,
                        "#ifndef STRPOOL\n"
                        "#define STRPOOL\n"
                        "#endif\n"
                        "#ifndef _P\n"
                        "#define _P(x) (_strpool_ + ((x) - (const char*) 1))\n"
                        "#endif\n\n");


                if (ifname)
                        fprintf(out, "#line 1 \"%s\"\n", ifname);

                dump_text(out, first);
                fwrite(remain, 1, remain_size, out);

                free_items(first);
        }

        free(remain);

        return 0;
}

int main(int argc, char *argv[]) {
        int ret;
        FILE *in = NULL, *out = NULL;

        if (argc > 1) {
                if (!(in = fopen(argv[1], "r"))) {
                        fprintf(stderr, "Failed to open '%s': %s\n", argv[1], strerror(errno));
                        return 1;
                }
        } else
                in = stdin;

        if (argc > 2) {
                if (!(out = fopen(argv[2], "w"))) {
                        fprintf(stderr, "Failed to open '%s': %s\n", argv[2], strerror(errno));
                        return 1;
                }
        } else
                out = stdout;

        if (process(in, out, argc > 1 ? argv[1] : NULL) < 0)
                goto finish;

        ret = 0;

finish:

        if (in != stdin)
                fclose(in);

        if (out != stdout)
                fclose(out);

        return ret;
}
