"""The frame compare as tests: one manual Bazel test per registered oracle suite.

`//sim:frames_<suite>_test` runs the suite through `replay_suite.py --frames` (es2sim --headless with the frame
capture and its region hashes), compares the capture with the reference manifest of DBSIM's capture
(`dbsim_frames.py regions`) and checks every kind of difference against the known-differences list
sim/tests/frames_known.py (sim/tests/frames_suite.py; own_process.md, "the frame compare as tests"). Tagged `manual` like the replay
tests (they need ES2_DATA_ROOT, the goldens and the manifests in es2-data/logs); `frames_all` runs them all.
"""

load("//bazel/defs:python_test.bzl", "host_python_test")
load(":replay_tests.bzl", "SUITES")

def frames_suite_tests():
    """One `frames_<suite>_test` per suite of SUITES plus the manual test_suite `frames_all`."""
    for suite in SUITES:
        host_python_test(
            name = "frames_%s_test" % suite,
            main = "tests/frames_suite.py",
            data = [
                ":es2sim",
                "tests/frames_known.py",
                "tests/replay_suite.py",
                "//tools/reference-game:oracle-suites.json",
                "//tools/reference-game:oracle_suite_inputs",
                "//tools/reference-game/scripts:oracle_regression.py",
                "//tools/reference-game/scripts:mission_watch_report.py",
                "//tools/reference-game/scripts:dbsim_frames.py",
                "//tools/reference-game/scripts:presenter_capture.py",
                # The goldens and the reference manifests are read from es2-data at run time, the CD from
                # ES2_DATA_ROOT; "no-cache" because none of them is tracked.
                "@es2_data//:reference_binaries",
            ],
            args = [
                "--sim",
                "$(rootpath :es2sim)",
                "--registry",
                "$(rootpath //tools/reference-game:oracle-suites.json)",
                "--oracle-script",
                "$(rootpath //tools/reference-game/scripts:oracle_regression.py)",
                "--replay",
                "$(rootpath tests/replay_suite.py)",
                "--frames-script",
                "$(rootpath //tools/reference-game/scripts:dbsim_frames.py)",
                "--suite",
                suite,
            ],
            size = "enormous",
            tags = ["manual", "local", "no-cache", "no-remote", "exclusive"],
            target_compatible_with = ["@platforms//os:windows", "@platforms//cpu:x86_32"],
        )
    native.test_suite(
        name = "frames_all",
        tests = [":frames_%s_test" % s for s in SUITES],
        tags = ["manual"],
    )
