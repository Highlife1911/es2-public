"""The replay tests with es2sim's wasm build over the built site.

`//sim:replay_site_<suite>_test` runs tests/replay_site.py: the replay of replay_wasm_tests.bzl, but host.mjs serves
the game's files from a site that tools/web/build_site.py builds from the CD below ES2_DATA_ROOT (or the site in
ES2_REPLAY_SITE), with unfetched bytes poisoned (docs/web_build.md section 16). `replay_site_all`
runs them all. Manual like the native tests.
"""

load("//bazel/defs:python_test.bzl", "host_python_test")
load("//bazel/platforms:host_tools.bzl", "HOST_NODE")
load(":replay_tests.bzl", "SUITES")
load(":replay_wasm_tests.bzl", "ES2SIM_WASM", "ES2SIM_WASM_JS")

_DATA = [
    "tests/replay_suite.py",
    "//tools/web:build_site.py",
    "//tools/web:zstd_pack",
    "//web:vfs_wasm",
    "//web:vfs_wasm_js",
    "//web/node:host.mjs",
    "//web/node:host_files",
    "//tools/reference-game:oracle-suites.json",
    "//tools/reference-game:oracle_suite_inputs",
    "//tools/reference-game/scripts:oracle_regression.py",
    "//tools/reference-game/scripts:mission_watch_report.py",
    # The goldens and launch sets are read from es2-data at run time, the CD from ES2_DATA_ROOT; "no-cache"
    # because neither is tracked.
    "@es2_data//:reference_binaries",
]

_ARGS = [
    "--build-site",
    "$(rootpath //tools/web:build_site.py)",
    "--zstd-pack",
    "$(rootpath //tools/web:zstd_pack)",
    "--codec",
    "$(rootpath //web:vfs_wasm_js)",
    "--registry",
    "$(rootpath //tools/reference-game:oracle-suites.json)",
    "--oracle-script",
    "$(rootpath //tools/reference-game/scripts:oracle_regression.py)",
]

def replay_site_suite_tests():
    """One `replay_site_<suite>_test` per suite of SUITES plus the manual test_suite `replay_site_all`."""
    if ES2SIM_WASM == None:
        return
    for suite in SUITES:
        host_python_test(
            name = "replay_site_%s_test" % suite,
            main = "tests/replay_site.py",
            data = [ES2SIM_WASM, ES2SIM_WASM_JS] + _DATA,
            args = _ARGS + ["--suite", suite, "--sim-argv", HOST_NODE, "$(rootpath //web/node:host.mjs)",
                            "$(rootpath %s)" % ES2SIM_WASM_JS],
            size = "enormous",
            tags = ["manual", "local", "no-cache", "no-remote", "exclusive"],
            target_compatible_with = ["@platforms//os:windows"],
        )
    native.test_suite(
        name = "replay_site_all",
        tests = [":replay_site_%s_test" % s for s in SUITES],
        tags = ["manual"],
    )
