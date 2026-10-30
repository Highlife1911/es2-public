#include "launch_logic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static const char *line_of(es2l_launch l) {
    static char text[512];
    if (!es2l_command_line(&l, text, sizeof text)) return "<overflow>";
    return text;
}

static void parsing(void) {
    es2l_options o;
    es2l_parse("", &o);
    CHECK(!o.s && !o.a && !o.l && !o.sprunknown && !o.force_sim && !o.r_given && !strcmp(o.r_buffer, "-"));
    es2l_parse("-s  -a -L -s", &o);              /* repeated spaces; -s toggles twice */
    CHECK(!o.s && o.a && o.l);
    es2l_parse("-S -SPRUNKNOWNX -sprunknown -SPRUNKNOWN", &o);
    CHECK(o.sprunknown && !o.force_sim);
    es2l_parse("-X -SPRUNKNOWN -Xyz", &o);       /* -X counts only after -SPRUNKNOWN */
    CHECK(o.sprunknown && o.force_sim);
    es2l_parse("-SPRUNKNOWN -X -X", &o);
    CHECK(!o.force_sim);
    es2l_parse("-rabc s -rx -D123 x-s", &o);     /* tokens without a leading dash are ignored */
    CHECK(o.r_given && !strcmp(o.r_buffer, "-rabcrx") && !o.s && !o.r_overflow);
    es2l_parse("-r123456789 -r123456789", &o);   /* 1 + 10 + 10 > 19 characters */
    CHECK(o.r_overflow && strlen(o.r_buffer) == ES2L_R_BUFFER - 1);
    es2l_parse("-\t-s", &o);                     /* a tab does not separate tokens */
    CHECK(!o.s);
    es2l_parse(NULL, &o);
    CHECK(!o.s && !strcmp(o.r_buffer, "-"));
}

static void language(void) {
    CHECK(es2l_language(-1) == 0);
    CHECK(es2l_language('E') == 0);
    CHECK(es2l_language('G') == 'G');
    CHECK(es2l_language('F') == 'F');
    CHECK(es2l_language(0) == 0);
}

static void memory(void) {
    const uint32_t big = 0x10000000u, n = 20000000u;
    CHECK(es2l_memory_check(0x739fffu, big, big, n) == 0);
    CHECK(es2l_memory_check(0x73a000u, 0, big, n) == 1);
    CHECK(es2l_memory_check(0x73a000u, 0x7a11ffu, big, n) == 2);
    CHECK(es2l_memory_check(0x73a000u, 0x7a1200u, 0, 12000000u) == -1);   /* limit 12,000,000 == available */
    CHECK(es2l_memory_check(0x73a000u, 0x7a1200u, 0, 11999999u) == 2);
    CHECK(es2l_memory_check(0xdabfffu, big, 0, 12000000u) == -1);
    CHECK(es2l_memory_check(0xdac000u, big, 0, 12000000u) == 2);           /* limit rises to 18,000,000 */
    CHECK(es2l_memory_check(0xdac000u, big, 0, 18000000u) == -1);
    CHECK(es2l_memory_check(0x7fffffffu, 0x7fffffffu, 0x7fffffffu, n) == 2); /* signed wrap of the sum */
    CHECK(es2l_memory_check(0x7fffffffu, 0x7fffffffu, 0x7fffffffu - n, n) == -1);
}

static void loop(void) {
    es2l_options o;
    int32_t counter = 0;
    es2l_parse("", &o);
    CHECK(es2l_next(0, &o, 'G', &counter).program == ES2L_QUIT);
    CHECK(es2l_next(7, &o, 'G', &counter).program == ES2L_QUIT);
    CHECK(es2l_next(-1, &o, 'G', &counter).program == ES2L_QUIT);
    for (int32_t s = 1; s <= 6; ++s) {
        es2l_launch l = es2l_next(s, &o, 'G', &counter);
        CHECK(l.program == ((s == 2 || s == 5) ? ES2L_SIM : ES2L_SHELL));
    }
    CHECK(counter == 2);
    counter = 0;
    CHECK(!strcmp(line_of(es2l_next(1, &o, 'G', &counter)), "dummy -eggplant -X1 -G"));
    CHECK(!strcmp(line_of(es2l_next(2, &o, 'G', &counter)), "dummy -eggplant -X2 -R0 -G"));
    CHECK(!strcmp(line_of(es2l_next(5, &o, 'G', &counter)), "dummy -eggplant -X5 -R1 -D -G"));
    CHECK(!strcmp(line_of(es2l_next(4, &o, 0, &counter)), "dummy -eggplant -X4"));
    es2l_parse("-s -a -L -rabc -SPRUNKNOWN", &o);
    counter = 0;
    CHECK(!strcmp(line_of(es2l_next(3, &o, 'F', &counter)), "dummy -eggplant -X3 -s -a -@ -F"));
    CHECK(!strcmp(line_of(es2l_next(5, &o, 'F', &counter)), "dummy -eggplant -X5 -R0 -s -D -l -rabc -SPRUNKNOWN -F"));
    es2l_parse("-SPRUNKNOWN -X", &o);
    counter = 0;
    es2l_launch forced = es2l_next(1, &o, 0, &counter);
    CHECK(forced.program == ES2L_SIM && !strcmp(line_of(forced), "dummy -eggplant -X2 -R0 -SPRUNKNOWN"));
    CHECK(es2l_next(0, &o, 0, &counter).program == ES2L_QUIT);  /* state 0 ends even when forced */
    char small[8];
    es2l_parse("", &o);
    es2l_launch l = es2l_next(1, &o, 'G', &counter);
    CHECK(!es2l_command_line(&l, small, sizeof small));
}

static void strings(void) {
    /* Own synthetic content: group 0 with two strings, group 1 with one. */
    static const uint8_t ok[] = {
        0, 0, 0, 0,
        2, 0, 3, 0, 'a', 'b', 0, 0, 2, 0, 'c', 0, 7,
        1, 0, 4, 0, 'x', 'y', 'z', 0, 0,
    };
    uint8_t data[sizeof ok];
    memcpy(data, ok, sizeof ok);
    data[0] = (uint8_t)(sizeof ok - 4);
    es2l_strings s;
    CHECK(es2l_parse_strings(data, sizeof data, &s));
    CHECK(!strcmp(s.group0[0], "ab") && !strcmp(s.group0[1], "c") && !s.group0[2] && !strcmp(s.group1[0], "xyz"));
    CHECK(!es2l_parse_strings(data, sizeof data - 1, &s));        /* body size beyond the file */
    data[0] = (uint8_t)(sizeof ok - 5);
    CHECK(!es2l_parse_strings(data, sizeof data, &s));            /* truncated body */
    data[0] = (uint8_t)(sizeof ok - 4);
    data[10] = 'X';
    CHECK(!es2l_parse_strings(data, sizeof data, &s));            /* length must include the NUL */
    data[10] = 0;
    CHECK(!es2l_parse_strings(data, 3, &s));
    /* More strings than slots (German error.str: 12 in group 0): framed, extra not kept. */
    static const uint8_t many[] = {
        0, 0, 0, 0,
        8, 0, 2, 0, 'a', 0, 0, 2, 0, 'b', 0, 0, 2, 0, 'c', 0, 0, 2, 0, 'd', 0, 0,
              2, 0, 'e', 0, 0, 2, 0, 'f', 0, 0, 2, 0, 'g', 0, 0, 2, 0, 'h', 0, 0,
        1, 0, 2, 0, 'z', 0, 0,
    };
    uint8_t more[sizeof many];
    memcpy(more, many, sizeof many);
    more[0] = (uint8_t)(sizeof many - 4);
    CHECK(es2l_parse_strings(more, sizeof more, &s));
    CHECK(!strcmp(s.group0[0], "a") && !strcmp(s.group0[5], "f") && !strcmp(s.group1[0], "z"));
    more[4] = 9;                                                    /* claims one string too many */
    CHECK(!es2l_parse_strings(more, sizeof more, &s));
}

/* Opt-in: the CD's error string files (the installer copies the localized
 * one to ERROR.STR). Only framing is checked; no text is compared or kept. */
static void real_strings(void) {
    const char *root = getenv("ES2_DATA_ROOT");
    if (!root || !*root) { printf("ES2_DATA_ROOT not set: real error.str files skipped\n"); return; }
    static const char *const files[] = {"ERROR.STR", "ENGLISH/ERROR.ENG", "FRENCH/ERROR.FRE", "GERMAN/ERROR.GER"};
    static uint8_t data[1 << 16];
    for (unsigned i = 0; i < sizeof files / sizeof files[0]; ++i) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", root, files[i]);
        FILE *f = fopen(path, "rb");
        CHECK(f != NULL);
        if (!f) continue;
        size_t n = fread(data, 1, sizeof data, f);
        fclose(f);
        es2l_strings s;
        CHECK(es2l_parse_strings(data, n, &s));
        CHECK(s.group0[0] && s.group0[1] && s.group0[2] && s.group1[0]);
    }
}

/* Shell replacement command line and exit-code rule. */
static void replacement(void) {
    es2l_options o;
    int32_t counter = 0;
    es2l_parse("-s -SPRUNKNOWN", &o);
    es2l_launch shell = es2l_next(3, &o, 'G', &counter);
    char text[128];
    CHECK(es2l_replacement_command("\"C:\\py.exe\" -B chain.py step --state \"D:\\s 1\"", &shell, text, sizeof text));
    CHECK(!strcmp(text, "\"C:\\py.exe\" -B chain.py step --state \"D:\\s 1\" dummy -eggplant -X3 -s -@ -G"));
    CHECK(es2l_replacement_command("x", &shell, text, 31));             /* "x dummy -eggplant -X3 -s -@ -G" + NUL */
    CHECK(!strcmp(text, "x dummy -eggplant -X3 -s -@ -G"));
    CHECK(!es2l_replacement_command("x", &shell, text, 30) && text[0] == 0);
    CHECK(!es2l_replacement_command("", &shell, text, sizeof text));
    CHECK(!es2l_replacement_command(NULL, &shell, text, sizeof text));
    CHECK(!es2l_replacement_command("0123456789", &shell, text, 11));
    CHECK(counter == 0);                                                  /* a shell start does not count */
    CHECK(es2l_after_replacement(2) == 2);
    CHECK(es2l_after_replacement(5) == 5);                                /* VIEW DEMO */
    static const int32_t stops[] = {0, 1, 3, 4, 6, -1, 7, 259};
    for (unsigned i = 0; i < sizeof stops / sizeof stops[0]; ++i) CHECK(es2l_after_replacement(stops[i]) == 0);
    /* Console window for the interactive menu only on request. */
    CHECK(!es2l_replacement_console(NULL) && !es2l_replacement_console("") && !es2l_replacement_console("0"));
    CHECK(es2l_replacement_console("1") && es2l_replacement_console("yes"));
}

/* The own DBSIM process gets DBSIM's argv without "dummy" as one
 * --arguments value; the prefix comes from the environment, else the INI. */
static void sim_replacement(void) {
    es2l_options o;
    int32_t counter = 4;
    es2l_parse("-s -L -SPRUNKNOWN", &o);
    es2l_launch sim = es2l_next(5, &o, 'G', &counter);
    char text[160];
    CHECK(es2l_sim_command("\"D:\\i 1\\ES2SIM\\es2sim.exe\" --headless", &sim, text, sizeof text));
    CHECK(!strcmp(text, "\"D:\\i 1\\ES2SIM\\es2sim.exe\" --headless --arguments \"-eggplant -X5 -R4 -s -D -l -SPRUNKNOWN -G\""));
    es2l_launch plain = es2l_next(2, &o, 0, &counter);
    static const char want[] = "x --arguments \"-eggplant -X2 -R5 -s -l -SPRUNKNOWN\"";
    CHECK(es2l_sim_command("x", &plain, text, sizeof want) && !strcmp(text, want));  /* exactly fits */
    CHECK(!es2l_sim_command("x", &plain, text, sizeof want - 1) && text[0] == 0);
    CHECK(!es2l_sim_command("x", &plain, text, 5) && text[0] == 0);
    CHECK(!es2l_sim_command("", &plain, text, sizeof text) && !es2l_sim_command(NULL, &plain, text, sizeof text));
    /* -r tokens are the user's text: a quote refuses, trailing backslashes are doubled. */
    es2l_parse("-rab\\\\", &o);
    es2l_launch slash = es2l_next(2, &o, 0, &counter);
    CHECK(es2l_sim_command("x", &slash, text, sizeof text) && !strcmp(text, "x --arguments \"-eggplant -X2 -R6 -rab\\\\\\\\\""));
    es2l_parse("-ra\"b", &o);
    es2l_launch quote = es2l_next(2, &o, 0, &counter);
    CHECK(!es2l_sim_command("x", &quote, text, sizeof text) && text[0] == 0);
    es2l_launch none;
    memset(&none, 0, sizeof none);
    none.argc = 1;
    CHECK(!es2l_sim_command("x", &none, text, sizeof text));
    CHECK(counter == 8);
    CHECK(!strcmp(es2l_sim_prefix("env", "ini"), "env") && !strcmp(es2l_sim_prefix("", "ini"), "ini") &&
          !strcmp(es2l_sim_prefix(NULL, "ini"), "ini"));
    CHECK(!es2l_sim_prefix(NULL, NULL) && !es2l_sim_prefix("", "") && !es2l_sim_prefix(NULL, ""));
}

int main(void) {
    parsing();
    language();
    memory();
    loop();
    strings();
    real_strings();
    replacement();
    sim_replacement();
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
