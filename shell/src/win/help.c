/* The online manual (shell/src/win/help.h). Contract:
 * docs/reverse-engineering/vshell_ui.md, section 21. */
#include "help.h"
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>

int es2_help_policy_parse(const char *value) {
    if (!value || !value[0] || !strcmp(value, "open")) return ES2_HELP_POLICY_OPEN;
    if (!strcmp(value, "text")) return ES2_HELP_POLICY_TEXT;
    if (!strcmp(value, "hide")) return ES2_HELP_POLICY_HIDE;
    return -1;
}

/* Original: VSHELL.EXE 0x431eac..0x431ee6: the byte minus 0x45 selects the
 * string at 0x47398d, 0x4739a2 or 0x4739b6; any other byte reaches the
 * assertion at 0x431efc and continues with an unset buffer. */
const char *es2_help_relative(int letter) {
    switch (letter) {
    case 'E': return "english\\es2guide.hlp";
    case 'F': return "french\\es2guide.hlp";
    case 'G': return "german\\es2guide.hlp";
    default: return NULL;
    }
}

int es2_help_path(char *out, size_t capacity, const char *cd_root, int letter) {
    const char *relative = es2_help_relative(letter);
    size_t root = cd_root ? strlen(cd_root) : 0;
    int separator = root && cd_root[root - 1] != '\\' && cd_root[root - 1] != '/';
    int n;
    if (!relative || !capacity) return 0;
    n = snprintf(out, capacity, "%s%s%s", cd_root ? cd_root : "", separator ? "\\" : "", relative);
    return n > 0 && (size_t)n < capacity;
}

void es2_help_show(const es2_help_ops *ops, es2_help_policy policy, const char *cd_root, es2_help_result *out) {
    memset(out, 0, sizeof *out);
    out->letter = -1;
    if (policy == ES2_HELP_POLICY_HIDE) {
        out->outcome = ES2_HELP_HIDDEN;
        return;
    }
    out->letter = ops->language ? ops->language(ops->ctx) : -1;
    if (!es2_help_path(out->path, sizeof out->path, cd_root, out->letter)) {
        out->path[0] = 0;
        out->outcome = es2_help_relative(out->letter) ? ES2_HELP_TOO_LONG
                       : out->letter < 0 ? ES2_HELP_NO_FILE : ES2_HELP_BAD_LANGUAGE;
        return;
    }
    if (!ops->exists || !ops->exists(ops->ctx, out->path)) {
        out->outcome = ES2_HELP_MISSING;
        return;
    }
    if (policy == ES2_HELP_POLICY_TEXT) {
        out->outcome = ES2_HELP_TEXT_ONLY;
        return;
    }
    out->outcome = ops->open && ops->open(ops->ctx, out->path) ? ES2_HELP_OPENED : ES2_HELP_NO_VIEWER;
}

/* Original: VSHELL.EXE 0x408d90..0x408dbf (src\utility.cpp, line 0xb7, the
 * file name as text) and 0x431efc..0x431f1a (src\WMAIN.CPP, line 0x104, "online
 * help not found."): 0x44e560 at severity 4, formatted by 0x44e6a8 with the
 * format strings at 0x47a689 and 0x47a6a5. */
int es2_help_assertion(es2_help_outcome outcome, char *out, size_t capacity) {
    int n;
    if (capacity) out[0] = 0;
    if (outcome == ES2_HELP_NO_FILE)
        n = snprintf(out, capacity, "Assert::err: Line %d in \"%s\": %s", 0xb7, "src\\utility.cpp", "data\\language.cfg");
    else if (outcome == ES2_HELP_BAD_LANGUAGE)
        n = snprintf(out, capacity, "Assert::err: Line %d in \"%s\": %s", 0x104, "src\\WMAIN.CPP", "online help not found.");
    else
        return 0;
    return n > 0 && (size_t)n < capacity;
}

int es2_help_win_exists(void *ctx, const char *path) {
    DWORD attributes = GetFileAttributesA(path);
    (void)ctx;
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

int es2_help_win_open(void *ctx, const char *path) {
    SHELLEXECUTEINFOA info;
    (void)ctx;
    memset(&info, 0, sizeof info);
    info.cbSize = sizeof info;
    /* no error dialogs (a missing association is the caller's text) and no
     * asynchronous start that would outlive the thread */
    info.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    info.lpVerb = "open";
    info.lpFile = path;
    info.nShow = SW_SHOWNORMAL;
    /* window-lint: allow visible on purpose: the viewer for the manual the player asked for */
    return ShellExecuteExA(&info) ? 1 : 0;
}
