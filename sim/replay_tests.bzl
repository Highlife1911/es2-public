"""The own-process replay tests: one manual Bazel test per registered oracle suite.

`//sim:replay_<suite>_test` runs `es2sim.exe --headless` on the suite's input journal and compares the tick log, the
watches, the return files and the exit code with the golden in `es2-data/oracle/golden` (sim/tests/replay_suite.py;
docs/reference-game/oracle_regression.md, "The own-process replay"). The tests are tagged `manual`: they need the
goldens and take up to 15 minutes each (all 42 identical), so `bazel test //...` leaves them out; name them
to run them. `SUITES` is the registry's list in
registry order; `//sim:replay_suite_test` fails when the two differ.
"""

load("//bazel/defs:python_test.bzl", "host_python_test")

SUITES = [
    "acceptance-60s",
    "c1_01-success",
    "c1_01-collision",
    "c3_09-loadout-b",
    "c4_08-razor-plasma",
    "c3_10-loadout-hercs",
    "c1_01-cockpit-keys",
    "c3_10-own-building-fire",
    "c1_01-free-camera",
    "c1_01-mouse",
    "c1_01-keys",
    "c1_02-probe",
    "c1_08-escort-route",
    "c1_01-cheat-destroy",
    "c3_09-wingman-orders",
    "c5_05-moon-missiles",
    "c3_09-orders-matrix",
    "c5_05-orders-flyer",
    "c1_01-cheat-disable-leg",
    "c1_01-cheat-leader",
    "c1_01-cheat-keys",
    "c1_01-cheat-keys-plain",
    "c1_01-options-dialog",
    "c1_01-keys-extra",
    "c1_01-unarmed-keys",
    "c3_10-hunt-targ",
    "c4_08-slope-dive",
    "c4_05-convoy",
    "c1_08-scan",
    "c4_10-probe",
    "c1_06-cheat-disable",
    "c2_02-identify",
    "c1_01-tape-record",
    "train1-timers",
    "c1_01-joystick",
    "c4_08-joystick",
    "c1_01-cheat-disable-late",
    "c1_01-display-keys",
    "c3_10-detail-options",
    "c1_01-quit-dialog",
    "c1_01-tape-play",
    "c1_01-tape-demo",
]

def replay_suite_tests():
    """One `replay_<suite>_test` per suite plus the manual test_suite `replay_all`."""
    for suite in SUITES:
        host_python_test(
            name = "replay_%s_test" % suite,
            main = "tests/replay_suite.py",
            data = [
                ":es2sim",
                "//tools/reference-game:oracle-suites.json",
                "//tools/reference-game:oracle_suite_inputs",
                "//tools/reference-game/scripts:oracle_regression.py",
                "//tools/reference-game/scripts:mission_watch_report.py",
                # The goldens and launch sets are read from es2-data/oracle at run time (not Bazel inputs), the CD
                # from ES2_DATA_ROOT; "no-cache" because neither is tracked.
                "@es2_data//:reference_binaries",
            ],
            args = [
                "--sim",
                "$(rootpath :es2sim)",
                "--registry",
                "$(rootpath //tools/reference-game:oracle-suites.json)",
                "--oracle-script",
                "$(rootpath //tools/reference-game/scripts:oracle_regression.py)",
                "--suite",
                suite,
            ],
            size = "enormous",
            tags = ["manual", "local", "no-cache", "no-remote", "exclusive"],
            target_compatible_with = ["@platforms//os:windows", "@platforms//cpu:x86_32"],
        )
    native.test_suite(
        name = "replay_all",
        tests = [":replay_%s_test" % s for s in SUITES],
        tags = ["manual"],
    )
