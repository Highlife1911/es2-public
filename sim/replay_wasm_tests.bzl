"""The replay and frame tests with es2sim's wasm build under Node.

`//sim:replay_wasm_<suite>_test` and `//sim:frames_wasm_<suite>_test` are the tests of replay_tests.bzl and
frames_tests.bzl with `--sim-argv NODE web/node/host.mjs es2sim.js` in place of `--sim es2sim.exe`: the module runs
under Node with the "es2os" imports of web/node/os_host.mjs over node:fs, in the same installation clone and with the
same arguments as the native es2sim. `replay_wasm_all` and `frames_wasm_all` run them all. Manual like the native
tests.

`//sim:replay_window_wasm_<suite>_test` (`replay_window_wasm_all`) runs the windowed replay of a suite natively with
es2sim_window and through the web platform with es2sim_window's wasm build under web/node/sim_window_host.mjs, and
compares both tick logs with the golden and the two message traces with each other (sim/tests/replay_window_wasm.py).

`ES2SIM_WASM` is the wasm module (`//sim:es2sim_wasm`, a `wasm_binary`) and `ES2SIM_WASM_JS` the filegroup of its
`.js` (output group "js"); with `ES2SIM_WASM` None the macros create nothing. `ES2SIM_WINDOW_WASM` and
`ES2SIM_WINDOW_WASM_JS` are the windowed build's.
"""

load("//bazel/defs:python_test.bzl", "host_python_test")
load("//bazel/platforms:host_tools.bzl", "HOST_NODE")
load(":replay_tests.bzl", "SUITES")

ES2SIM_WASM = "//sim:es2sim_wasm"
ES2SIM_WASM_JS = "//sim:es2sim_wasm_js"
ES2SIM_WINDOW_WASM = "//sim:es2sim_window_wasm"
ES2SIM_WINDOW_WASM_JS = "//sim:es2sim_window_wasm_js"

# The suites of the native windowed replay (//sim:replay_window_<suite>_test).
WINDOW_SUITES = ["acceptance-60s", "c1_01-success"]

_ORACLE_DATA = [
    "//tools/reference-game:oracle-suites.json",
    "//tools/reference-game:oracle_suite_inputs",
    "//tools/reference-game/scripts:oracle_regression.py",
    "//tools/reference-game/scripts:mission_watch_report.py",
    # The goldens and launch sets are read from es2-data at run time, the CD from ES2_DATA_ROOT; "no-cache"
    # because neither is tracked.
    "@es2_data//:reference_binaries",
]

_ORACLE_ARGS = [
    "--registry",
    "$(rootpath //tools/reference-game:oracle-suites.json)",
    "--oracle-script",
    "$(rootpath //tools/reference-game/scripts:oracle_regression.py)",
]

_TAGS = ["manual", "local", "no-cache", "no-remote", "exclusive"]

def _sim_argv():
    # Last: --sim-argv ends the driver's options.
    return ["--sim-argv", HOST_NODE, "$(rootpath //web/node:host.mjs)", "$(rootpath %s)" % ES2SIM_WASM_JS]

def _sim_data():
    return [ES2SIM_WASM, ES2SIM_WASM_JS, "//web/node:host.mjs", "//web/node:host_files"]

def replay_wasm_suite_tests():
    """One `replay_wasm_<suite>_test` per suite of SUITES plus the manual test_suite `replay_wasm_all`."""
    if ES2SIM_WASM == None:
        return
    for suite in SUITES:
        host_python_test(
            name = "replay_wasm_%s_test" % suite,
            main = "tests/replay_suite.py",
            data = _sim_data() + _ORACLE_DATA,
            args = _ORACLE_ARGS + ["--suite", suite] + _sim_argv(),
            size = "enormous",
            tags = _TAGS,
            target_compatible_with = ["@platforms//os:windows"],
        )
    native.test_suite(
        name = "replay_wasm_all",
        tests = [":replay_wasm_%s_test" % s for s in SUITES],
        tags = ["manual"],
    )

def frames_wasm_suite_tests():
    """One `frames_wasm_<suite>_test` per suite of SUITES plus the manual test_suite `frames_wasm_all`."""
    if ES2SIM_WASM == None:
        return
    for suite in SUITES:
        host_python_test(
            name = "frames_wasm_%s_test" % suite,
            main = "tests/frames_suite.py",
            data = _sim_data() + _ORACLE_DATA + [
                "tests/frames_known.py",
                "tests/replay_suite.py",
                "//tools/reference-game/scripts:dbsim_frames.py",
                "//tools/reference-game/scripts:presenter_capture.py",
            ],
            args = _ORACLE_ARGS + [
                "--replay",
                "$(rootpath tests/replay_suite.py)",
                "--frames-script",
                "$(rootpath //tools/reference-game/scripts:dbsim_frames.py)",
                "--suite",
                suite,
            ] + _sim_argv(),
            size = "enormous",
            tags = _TAGS,
            target_compatible_with = ["@platforms//os:windows"],
        )
    native.test_suite(
        name = "frames_wasm_all",
        tests = [":frames_wasm_%s_test" % s for s in SUITES],
        tags = ["manual"],
    )

def replay_window_wasm_suite_tests():
    """One `replay_window_wasm_<suite>_test` per suite of WINDOW_SUITES plus the manual test_suite `replay_window_wasm_all`."""
    if ES2SIM_WINDOW_WASM == None:
        return
    for suite in WINDOW_SUITES:
        host_python_test(
            name = "replay_window_wasm_%s_test" % suite,
            main = "tests/replay_window_wasm.py",
            data = _ORACLE_DATA + [
                "tests/replay_suite.py",
                ":es2sim_window",
                ":sdl_runtime",
                "//tools/reference-game/scripts:message_trace.py",
                ES2SIM_WINDOW_WASM,
                ES2SIM_WINDOW_WASM_JS,
                "//web/node:sim_window_host.mjs",
                "//web/node:sim_window_host_files",
            ],
            args = _ORACLE_ARGS + [
                "--replay",
                "$(rootpath tests/replay_suite.py)",
                "--message-trace-script",
                "$(rootpath //tools/reference-game/scripts:message_trace.py)",
                "--native",
                "$(rootpath :es2sim_window)",
                "--suite",
                suite,
                # Last: --sim-argv ends the driver's options.
                "--sim-argv",
                HOST_NODE,
                "$(rootpath //web/node:sim_window_host.mjs)",
                "$(rootpath %s)" % ES2SIM_WINDOW_WASM_JS,
            ],
            size = "enormous",
            tags = _TAGS,
            target_compatible_with = ["@platforms//os:windows", "@platforms//cpu:x86_32"],
        )
    native.test_suite(
        name = "replay_window_wasm_all",
        tests = [":replay_window_wasm_%s_test" % s for s in WINDOW_SUITES],
        tags = ["manual"],
    )
