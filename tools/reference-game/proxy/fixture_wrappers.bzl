"""Generate tiny C test adapters; the existing own-code fixtures stay unchanged."""

_DIRECTORY_HELPER = r"""
static int es2_bazel_output_directory(WCHAR directory[MAX_PATH]) {
    DWORD length = GetEnvironmentVariableW(L"TEST_TMPDIR", directory, MAX_PATH);
    if (length == 0)
        length = GetEnvironmentVariableW(L"TEST_UNDECLARED_OUTPUTS_DIR", directory, MAX_PATH);
    /* Existing fixtures use MAX_PATH buffers and append their own filenames. */
    if (length == 0 || length >= MAX_PATH - 32) {
        fputs("Missing or too-long Bazel test output directory\n", stderr);
        return 1;
    }
    if (!CreateDirectoryW(directory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        fprintf(stderr, "Cannot create test directory: %lu\n", (unsigned long)GetLastError());
        return 1;
    }
    return 0;
}
"""

_TICK_MAIN = r"""
int wmain(int argc, WCHAR **argv) {
    WCHAR directory[MAX_PATH], output[MAX_PATH];
    WCHAR *fixture_arguments[4];
    if (argc != 2 || es2_bazel_output_directory(directory)) return 2;
    int length = swprintf(output, MAX_PATH, L"%ls\\tick.jsonl", directory);
    if (length < 0 || length >= MAX_PATH) return 2;
    fixture_arguments[0] = argv[0];
    fixture_arguments[1] = argv[1];
    fixture_arguments[2] = output;
    fixture_arguments[3] = NULL;
    return es2_fixture_entry(3, fixture_arguments);
}
"""

_INPUT_MAIN = r"""
/* Record/replay and script/focus share files, never globals. Seed the requested
 * case in a separate process so every mode remains an independent cc_test. */
static int es2_bazel_prepare(const WCHAR *mode, const WCHAR *directory) {
    WCHAR executable[32768], command[32768];
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION process = {0};
    DWORD length = GetModuleFileNameW(NULL, executable, 32768), code = 1;
    if (length == 0 || length >= 32768) return 1;
    int written = swprintf(command, 32768, L"\"%ls\" --fixture-child %ls \"%ls\"",
                           executable, mode, directory);
    if (written < 0 || written >= 32768) return 1;
    startup.cb = sizeof(startup);
    if (!CreateProcessW(executable, command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, NULL, &startup, &process)) {
        fprintf(stderr, "Fixture prerequisite failed to start: %lu\n", (unsigned long)GetLastError());
        return 1;
    }
    DWORD wait = WaitForSingleObject(process.hProcess, 30000);
    if (wait == WAIT_OBJECT_0) {
        if (!GetExitCodeProcess(process.hProcess, &code)) code = 1;
    } else {
        /* This handle identifies only the own child created immediately above. */
        (void)TerminateProcess(process.hProcess, 124);
        (void)WaitForSingleObject(process.hProcess, 1000);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return code == 0 ? 0 : 1;
}

int wmain(int argc, WCHAR **argv) {
    WCHAR directory[MAX_PATH];
    WCHAR *fixture_arguments[4];
    if (argc == 4 && !wcscmp(argv[1], L"--fixture-child"))
        return es2_fixture_entry(3, argv + 1);
    if (argc != 2 || es2_bazel_output_directory(directory)) return 2;
    if (!wcscmp(argv[1], L"replay") && es2_bazel_prepare(L"record", directory)) return 1;
    if (!wcscmp(argv[1], L"focus") && es2_bazel_prepare(L"script", directory)) return 1;
    fixture_arguments[0] = argv[0];
    fixture_arguments[1] = argv[1];
    fixture_arguments[2] = directory;
    fixture_arguments[3] = NULL;
    return es2_fixture_entry(3, fixture_arguments);
}
"""

def _fixture_wrapper_impl(ctx):
    output = ctx.actions.declare_file(ctx.label.name + ".c")
    entry = _TICK_MAIN if ctx.attr.mode == "tick" else _INPUT_MAIN
    source = """/* Generated adapter for an unchanged own-code fixture. */
#define wmain es2_fixture_entry
#include "%s"
#undef wmain
""" % ctx.file.fixture.path
    ctx.actions.write(output, source + _DIRECTORY_HELPER + entry)
    return [DefaultInfo(files = depset([output]))]

fixture_wrapper = rule(
    implementation = _fixture_wrapper_impl,
    attrs = {
        "fixture": attr.label(allow_single_file = [".c"], mandatory = True),
        "mode": attr.string(values = ["tick", "input"], mandatory = True),
    },
)
