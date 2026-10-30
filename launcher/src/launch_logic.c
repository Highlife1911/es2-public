#include "launch_logic.h"
#include "es2/formats/str.h"
#include <stdio.h>
#include <string.h>

static void toggle(bool *flag) { *flag = !*flag; }

static void append_r(es2l_options *o, const char *token, size_t length) {
    size_t used = strlen(o->r_buffer);
    if (used + length >= sizeof o->r_buffer) { o->r_overflow = true; length = sizeof o->r_buffer - 1 - used; }
    memcpy(o->r_buffer + used, token, length);
    o->r_buffer[used + length] = '\0';
}

static void option(es2l_options *o, const char *token, size_t length) {
    if (length < 2 || token[0] != '-') return;
    const char *rest = token + 1;
    size_t rest_length = length - 1;
    switch (rest[0]) {
    case 's': toggle(&o->s); break;
    case 'a': toggle(&o->a); break;
    case 'L': toggle(&o->l); break;
    case 'r': o->r_given = true; append_r(o, rest, rest_length); break;
    case 'S':
        if (rest_length == 10 && !memcmp(rest, "SPRUNKNOWN", 10)) toggle(&o->sprunknown);
        break;
    case 'X': if (o->sprunknown) toggle(&o->force_sim); break;
    default: break; /* includes -D<n>, whose value the original discards */
    }
}

void es2l_parse(const char *line, es2l_options *o) {
    memset(o, 0, sizeof *o);
    o->r_buffer[0] = '-';
    if (!line) return;
    while (*line) {
        if (*line == ' ') { ++line; continue; }
        const char *end = strchr(line, ' ');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        option(o, line, length);
        line += length;
    }
}

char es2l_language(int first) {
    if (first < 0 || first == 'E') return 0;
    return (char)first;
}

int es2l_memory_check(uint32_t total_phys, uint32_t total_pagefile,
                      uint32_t avail_pagefile, uint32_t allocated) {
    if (total_phys < 0x73a000u) return 0;
    if (total_pagefile == 0) return 1;
    if (total_pagefile < 0x7a1200u) return 2;
    int32_t limit = total_phys >= 0xdac000u ? 18000000 : 12000000;
    int32_t available = (int32_t)(avail_pagefile + allocated); /* 32-bit wrap, signed compare */
    return limit > available ? 2 : -1;
}

static void add(es2l_launch *l, const char *text) {
    if (l->argc < ES2L_MAX_ARGS) snprintf(l->args[l->argc++], ES2L_ARG_TEXT, "%s", text);
}

static void add_number(es2l_launch *l, char letter, int32_t value) {
    if (l->argc < ES2L_MAX_ARGS) snprintf(l->args[l->argc++], ES2L_ARG_TEXT, "-%c%ld", letter, (long)value);
}

es2l_launch es2l_next(int32_t state, const es2l_options *o, char language, int32_t *counter) {
    es2l_launch l;
    memset(&l, 0, sizeof l);
    if (state == 0) { l.program = ES2L_QUIT; return l; }
    if (o->force_sim) state = 2;
    bool demo = false;
    switch ((uint32_t)state) {
    case 1: case 3: case 4: case 6: l.program = ES2L_SHELL; l.name = "vshell"; break;
    case 5: demo = true; /* fall through */
    case 2: l.program = ES2L_SIM; l.name = "dbsim"; break;
    default: l.program = ES2L_QUIT; return l;
    }
    char language_arg[3] = {'-', language, 0};
    add(&l, "dummy");
    add(&l, "-eggplant");
    add_number(&l, 'X', state);
    if (l.program == ES2L_SHELL) {
        if (o->s) add(&l, "-s");
        if (o->a) add(&l, "-a");
        if (o->sprunknown) add(&l, "-@");
    } else {
        add_number(&l, 'R', (*counter)++);
        if (o->s) add(&l, "-s");
        if (demo) add(&l, "-D");
        if (o->l) add(&l, "-l");
        if (o->r_given) add(&l, o->r_buffer);
        if (o->sprunknown) add(&l, "-SPRUNKNOWN");
    }
    if (language) add(&l, language_arg);
    return l;
}

bool es2l_command_line(const es2l_launch *l, char *out, size_t capacity) {
    size_t used = 0;
    if (!capacity) return false;
    out[0] = '\0';
    for (unsigned i = 0; i < l->argc; ++i) {
        size_t n = strlen(l->args[i]);
        if (used + (i ? 1 : 0) + n + 1 > capacity) return false;
        if (i) out[used++] = ' ';
        memcpy(out + used, l->args[i], n + 1);
        used += n;
    }
    return true;
}

/* error.str: the framing is the packed STR grammar of formats/str.h. ES.EXE
 * (0x401824, 0x4018ac) asks for the first two groups and does not request the
 * auxiliary pointer, hence ES2_STR_TAG_BYTE. It stores every string of a
 * group, even beyond its array (the German error.str has 12 in group 0); only
 * the first slots are ever read, so extra strings are framed but not kept. */
bool es2l_parse_strings(const uint8_t *d, size_t size, es2l_strings *out) {
    es2_span input = {d, size};
    es2_str str;
    es2_str_cursor cursor;
    memset(out, 0, sizeof *out);
    if (es2_str_parse(input, 2, ES2_STR_TAG_BYTE, &str, NULL) != ES2_OK) return false;
    if (es2_str_cursor_init(&str, &cursor, NULL) != ES2_OK) return false;
    for (unsigned g = 0; g < 2; ++g) {
        size_t count;
        if (es2_str_next_group(&cursor, &count, NULL) != ES2_OK) return false;
        for (size_t i = 0; i < count; ++i) {
            es2_str_string s;
            if (es2_str_next_string(&cursor, &s, NULL) != ES2_OK) return false;
            /* The text is followed by its NUL inside the input: a C string. */
            if (g == 0 && i < 6) out->group0[i] = (const char *)s.text.data;
            if (g == 1 && i < 3) out->group1[i] = (const char *)s.text.data;
        }
    }
    return true;
}

bool es2l_replacement_command(const char *prefix, const es2l_launch *l, char *out, size_t capacity) {
    if (!prefix || !*prefix || !capacity) return false;
    size_t n = strlen(prefix);
    if (n + 1 >= capacity) return false;
    memcpy(out, prefix, n);
    out[n] = ' ';
    if (!es2l_command_line(l, out + n + 1, capacity - n - 1)) { out[0] = '\0'; return false; }
    return true;
}

/* 5 (VIEW DEMO 0x431c24) starts DBSIM with -D as after VSHELL; its
 * 6 then starts the replacement with -X6 (state 6, es2l_next). */
int32_t es2l_after_replacement(int32_t code) { return code == 2 || code == 5 ? code : 0; }

bool es2l_replacement_console(const char *value) { return value && *value && strcmp(value, "0") != 0; }

/* The own DBSIM process (es2sim, sim/src/main.c) takes DBSIM's
 * arguments as one --arguments value and puts "DBSIM.EXE" in front of them
 * for the guest, so argv[0] ("dummy") is left out here; the guest's WinMain
 * tail is then the one DBSIM gets from ES.EXE. The value is one quoted
 * argument: a quote inside cannot be passed and refuses the start, and a run
 * of backslashes before the closing quote is doubled (the CRT's rule). */
bool es2l_sim_command(const char *prefix, const es2l_launch *l, char *out, size_t capacity) {
    static const char option[] = " --arguments \"";
    if (!prefix || !*prefix || !capacity || l->argc < 2) return false;
    out[0] = '\0';
    size_t used = strlen(prefix);
    if (used + sizeof option > capacity) return false;
    memcpy(out, prefix, used);
    memcpy(out + used, option, sizeof option);
    used += sizeof option - 1;
    size_t slashes = 0;
    for (unsigned i = 1; i < l->argc; ++i) {
        if (i > 1) { if (used + 1 >= capacity) goto full; out[used++] = ' '; }
        for (const char *c = l->args[i]; *c; ++c) {
            if (*c == '"') { out[0] = '\0'; return false; }
            if (used + 1 >= capacity) goto full;
            out[used++] = *c;
        }
    }
    for (size_t at = used; at > 0 && out[at - 1] == '\\'; --at) ++slashes;
    if (used + slashes + 2 > capacity) goto full;
    for (size_t i = 0; i < slashes; ++i) out[used++] = '\\';
    out[used++] = '"';
    out[used] = '\0';
    return true;
full:
    out[0] = '\0';
    return false;
}

const char *es2l_sim_prefix(const char *environment, const char *ini) {
    if (environment && *environment) return environment;
    return ini && *ini ? ini : NULL;
}
