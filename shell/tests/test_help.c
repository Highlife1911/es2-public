/* The online manual's decisions (shell/src/win/help.h) over injected
 * operations: no viewer is ever started and no file is touched. The Windows
 * operations are only checked on a missing file. */
#include "win/help.h"
#include <stdio.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr, "line %d: %s\n", __LINE__, #x); } } while (0)

typedef struct fake {
    int letter, exists, opens, language_calls, exists_calls, open_calls;
    char opened[ES2_HELP_PATH], asked[ES2_HELP_PATH];
} fake;
static int f_language(void *ctx) { fake *f = ctx; f->language_calls++; return f->letter; }
static int f_exists(void *ctx, const char *path) {
    fake *f = ctx;
    f->exists_calls++;
    snprintf(f->asked, sizeof f->asked, "%s", path);
    return f->exists;
}
static int f_open(void *ctx, const char *path) {
    fake *f = ctx;
    f->open_calls++;
    snprintf(f->opened, sizeof f->opened, "%s", path);
    return f->opens;
}
static es2_help_result run(fake *f, es2_help_policy policy, const char *root) {
    es2_help_ops ops = {f, f_language, f_exists, f_open};
    es2_help_result r;
    es2_help_show(&ops, policy, root, &r);
    return r;
}

static void policy_and_paths(void) {
    char path[ES2_HELP_PATH], tiny[8];
    CHECK(es2_help_policy_parse("open") == ES2_HELP_POLICY_OPEN);
    CHECK(es2_help_policy_parse("text") == ES2_HELP_POLICY_TEXT);
    CHECK(es2_help_policy_parse("hide") == ES2_HELP_POLICY_HIDE);
    CHECK(es2_help_policy_parse(NULL) == ES2_HELP_POLICY_OPEN && es2_help_policy_parse("") == ES2_HELP_POLICY_OPEN);
    CHECK(es2_help_policy_parse("keep") == -1 && es2_help_policy_parse("Text") == -1 && es2_help_policy_parse("HIDE") == -1);
    /* 0x431eac: the byte is compared with E, F, G exactly */
    CHECK(!strcmp(es2_help_relative('E'), "english\\es2guide.hlp"));
    CHECK(!strcmp(es2_help_relative('F'), "french\\es2guide.hlp"));
    CHECK(!strcmp(es2_help_relative('G'), "german\\es2guide.hlp"));
    CHECK(!es2_help_relative('e') && !es2_help_relative('g') && !es2_help_relative(0) && !es2_help_relative(-1));
    CHECK(!es2_help_relative('D') && !es2_help_relative('H') && !es2_help_relative(0x145));
    /* CD root + relative */
    CHECK(es2_help_path(path, sizeof path, "D:\\", 'E') && !strcmp(path, "D:\\english\\es2guide.hlp"));
    CHECK(es2_help_path(path, sizeof path, "D:", 'F') && !strcmp(path, "D:\\french\\es2guide.hlp"));
    CHECK(es2_help_path(path, sizeof path, "C:\\cd\\", 'G') && !strcmp(path, "C:\\cd\\german\\es2guide.hlp"));
    CHECK(es2_help_path(path, sizeof path, "C:/cd/", 'G') && !strcmp(path, "C:/cd/german\\es2guide.hlp"));
    CHECK(es2_help_path(path, sizeof path, "", 'E') && !strcmp(path, "english\\es2guide.hlp"));
    CHECK(es2_help_path(path, sizeof path, NULL, 'E') && !strcmp(path, "english\\es2guide.hlp"));
    CHECK(!es2_help_path(path, sizeof path, "D:\\", 'X'));
    CHECK(!es2_help_path(tiny, sizeof tiny, "D:\\", 'E'));          /* does not fit */
    CHECK(!es2_help_path(path, 0, "D:\\", 'E'));
    /* exactly fitting: the terminating NUL counts */
    {
        char exact[sizeof "english\\es2guide.hlp"], short_by_one[sizeof "english\\es2guide.hlp" - 1];
        CHECK(es2_help_path(exact, sizeof exact, "", 'E'));
        CHECK(!es2_help_path(short_by_one, sizeof short_by_one, "", 'E'));
    }
}

static void decisions(void) {
    fake f;
    es2_help_result r;
    /* open: language, existence, then the handler; the path is the CD's */
    memset(&f, 0, sizeof f);
    f.letter = 'G'; f.exists = 1; f.opens = 1;
    r = run(&f, ES2_HELP_POLICY_OPEN, "D:\\");
    CHECK(r.outcome == ES2_HELP_OPENED && r.letter == 'G' && !strcmp(r.path, "D:\\german\\es2guide.hlp"));
    CHECK(f.language_calls == 1 && f.exists_calls == 1 && f.open_calls == 1);
    CHECK(!strcmp(f.opened, "D:\\german\\es2guide.hlp") && !strcmp(f.asked, f.opened));
    /* no handler registered, or it did not start */
    f.opens = 0; f.open_calls = 0;
    r = run(&f, ES2_HELP_POLICY_OPEN, "D:\\");
    CHECK(r.outcome == ES2_HELP_NO_VIEWER && !strcmp(r.path, "D:\\german\\es2guide.hlp") && f.open_calls == 1);
    /* the file is not there: the handler is not started */
    memset(&f, 0, sizeof f);
    f.letter = 'E'; f.opens = 1;
    r = run(&f, ES2_HELP_POLICY_OPEN, "D:\\");
    CHECK(r.outcome == ES2_HELP_MISSING && !strcmp(r.path, "D:\\english\\es2guide.hlp") && f.open_calls == 0);
    /* policy text: the path is named, nothing is opened */
    f.exists = 1;
    r = run(&f, ES2_HELP_POLICY_TEXT, "D:\\");
    CHECK(r.outcome == ES2_HELP_TEXT_ONLY && !strcmp(r.path, "D:\\english\\es2guide.hlp") && f.open_calls == 0);
    f.exists = 0;
    r = run(&f, ES2_HELP_POLICY_TEXT, "D:\\");
    CHECK(r.outcome == ES2_HELP_MISSING && f.open_calls == 0);
    /* language.cfg unreadable or not E, F, G: no path, nothing asked of the system */
    for (int letter = -1; letter < 256; letter++) {
        if (letter == 'E' || letter == 'F' || letter == 'G') continue;
        memset(&f, 0, sizeof f);
        f.letter = letter; f.exists = 1; f.opens = 1;
        r = run(&f, ES2_HELP_POLICY_OPEN, "D:\\");
        CHECK(r.outcome == (letter < 0 ? ES2_HELP_NO_FILE : ES2_HELP_BAD_LANGUAGE) && r.letter == letter && !r.path[0]);
        CHECK(f.language_calls == 1 && f.exists_calls == 0 && f.open_calls == 0);
    }
    /* policy hide: not even language.cfg is read */
    memset(&f, 0, sizeof f);
    f.letter = 'E'; f.exists = 1; f.opens = 1;
    r = run(&f, ES2_HELP_POLICY_HIDE, "D:\\");
    CHECK(r.outcome == ES2_HELP_HIDDEN && r.letter == -1 && !r.path[0]);
    CHECK(f.language_calls == 0 && f.exists_calls == 0 && f.open_calls == 0);
    /* the three languages */
    for (int i = 0; i < 3; i++) {
        static const char letters[] = "EFG";
        static const char *names[] = {"english", "french", "german"};
        char expected[ES2_HELP_PATH];
        memset(&f, 0, sizeof f);
        f.letter = letters[i]; f.exists = 1; f.opens = 1;
        snprintf(expected, sizeof expected, "X:\\cd\\%s\\es2guide.hlp", names[i]);
        r = run(&f, ES2_HELP_POLICY_OPEN, "X:\\cd");
        CHECK(r.outcome == ES2_HELP_OPENED && !strcmp(f.opened, expected));
    }
    /* a root that makes the path too long */
    {
        char root[ES2_HELP_PATH + 8];
        memset(root, 'a', sizeof root);
        root[sizeof root - 1] = 0;
        memset(&f, 0, sizeof f);
        f.letter = 'E'; f.exists = 1; f.opens = 1;
        r = run(&f, ES2_HELP_POLICY_OPEN, root);
        CHECK(r.outcome == ES2_HELP_TOO_LONG && !r.path[0] && f.exists_calls == 0 && f.open_calls == 0);
    }
    /* missing operations behave as failures */
    {
        es2_help_ops none = {NULL, NULL, NULL, NULL};
        es2_help_show(&none, ES2_HELP_POLICY_OPEN, "D:\\", &r);
        CHECK(r.outcome == ES2_HELP_NO_FILE);
    }
    /* the assertion texts of VSHELL's report (0x44e6a8) */
    {
        char text[128], tiny[8];
        CHECK(es2_help_assertion(ES2_HELP_BAD_LANGUAGE, text, sizeof text) &&
              !strcmp(text, "Assert::err: Line 260 in \"src\\WMAIN.CPP\": online help not found."));
        CHECK(es2_help_assertion(ES2_HELP_NO_FILE, text, sizeof text) &&
              !strcmp(text, "Assert::err: Line 183 in \"src\\utility.cpp\": data\\language.cfg"));
        CHECK(!es2_help_assertion(ES2_HELP_OPENED, text, sizeof text) && !text[0]);
        CHECK(!es2_help_assertion(ES2_HELP_MISSING, text, sizeof text) && !es2_help_assertion(ES2_HELP_TOO_LONG, text, sizeof text));
        CHECK(!es2_help_assertion(ES2_HELP_BAD_LANGUAGE, tiny, sizeof tiny));
    }
}

static void windows_operations(void) {
    /* only the harmless one: a path that cannot exist */
    CHECK(!es2_help_os_exists(NULL, "Z:\\no\\such\\dir\\es2guide.hlp"));
    CHECK(!es2_help_os_exists(NULL, "."));   /* a directory is not the manual */
}

int main(void) {
    policy_and_paths();
    decisions();
    windows_operations();
    printf("help_test: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
