/* Portable parts of es2menu on synthetic inputs only. */
#include "menu.h"
#include <stdio.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr, "line %d: %s\n", __LINE__, #x); } } while (0)

static void arguments(void) {
    es2m_args a;
    char message[160];
    char *ok[] = {"es2menu", "--menu", "D:\\m", "dummy", "-eggplant", "-X3", "-s", "-@", "-G"};
    CHECK(es2m_parse_args(9, ok, &a, message, sizeof message) == 0);
    CHECK(!a.no_movies);
    char *movies_off[] = {"es2menu", "--menu", "m", "-X0", "-a"};
    CHECK(es2m_parse_args(5, movies_off, &a, message, sizeof message) == 0 && a.no_movies && a.state == 0);
    char *no_movies[] = {"es2menu", "--menu", "m", "--no-movies", "-X2"};
    CHECK(es2m_parse_args(5, no_movies, &a, message, sizeof message) == 0 && a.no_movies && a.state == 2);
    CHECK(es2m_parse_args(9, ok, &a, message, sizeof message) == 0);
    CHECK(!strcmp(a.menu_dir, "D:\\m") && !a.input && a.state == 3 && !strcmp(a.language, "GER"));
    char *script[] = {"es2menu", "--menu", "m", "--input", "in.txt", "dummy", "-eggplant", "-X1"};
    CHECK(es2m_parse_args(8, script, &a, message, sizeof message) == 0 && !strcmp(a.input, "in.txt") &&
          a.state == 1 && !strcmp(a.language, "ENG"));
    char *french[] = {"es2menu", "--menu", "m", "-X6", "-F"};
    CHECK(es2m_parse_args(5, french, &a, message, sizeof message) == 0 && !strcmp(a.language, "FRE") && a.state == 6);
    char *no_state[] = {"es2menu", "--menu", "m", "dummy"};
    CHECK(es2m_parse_args(4, no_state, &a, message, sizeof message) == -1);
    char *two[] = {"es2menu", "--menu", "m", "-X1", "-X3"};
    CHECK(es2m_parse_args(5, two, &a, message, sizeof message) == -1);
    char *bad[] = {"es2menu", "--menu", "m", "-X1", "-Q"};
    CHECK(es2m_parse_args(5, bad, &a, message, sizeof message) == -1 && strstr(message, "-Q"));
    char *no_menu[] = {"es2menu", "-X1"};
    CHECK(es2m_parse_args(2, no_menu, &a, message, sizeof message) == -1);
    char *junk[] = {"es2menu", "--menu", "m", "-X1x"};
    CHECK(es2m_parse_args(4, junk, &a, message, sizeof message) == -1);
}

static void catalogue(void) {
    /* Three entries "ab", "cd\r\n", "" (own bytes). */
    uint8_t c[8 + 6 + 3 + 5 + 1] = {3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 8, 0, 'a', 'b', 0, 'c', 'd', '\r', '\n', 0, 0};
    es2_span t;
    es2_error e;
    CHECK(es2m_bin_string((es2_span){c, sizeof c}, 0, &t, &e) == ES2_OK && t.size == 2 && !memcmp(t.data, "ab", 2));
    CHECK(es2m_bin_string((es2_span){c, sizeof c}, 1, &t, &e) == ES2_OK && t.size == 2 && !memcmp(t.data, "cd", 2));
    CHECK(es2m_bin_string((es2_span){c, sizeof c}, 2, &t, &e) == ES2_OK && t.size == 0);
    CHECK(es2m_bin_raw((es2_span){c, sizeof c}, 1, &t, &e) == ES2_OK && t.size == 4 && !memcmp(t.data, "cd\r\n", 4));
    CHECK(es2m_bin_raw((es2_span){c, sizeof c}, 3, &t, &e) == ES2_ERR_COUNT);
    CHECK(es2m_bin_string((es2_span){c, sizeof c}, 3, &t, &e) == ES2_ERR_COUNT);
    CHECK(es2m_bin_string((es2_span){c, sizeof c - 1}, 2, &t, &e) == ES2_ERR_OFFSET);
    c[sizeof c - 2] = 'x';   /* entry 1 without its NUL runs into entry 2's NUL: still bounded */
    CHECK(es2m_bin_string((es2_span){c, sizeof c}, 1, &t, &e) == ES2_OK && t.size == 5);
    c[sizeof c - 1] = 'y';
    CHECK(es2m_bin_string((es2_span){c, sizeof c}, 1, &t, &e) == ES2_ERR_STRING);
    CHECK(es2m_bin_string((es2_span){c, 7}, 0, &t, &e) == ES2_ERR_TRUNCATED);
}

int main(void) {
    arguments();
    catalogue();
    printf("menu synthetic: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
