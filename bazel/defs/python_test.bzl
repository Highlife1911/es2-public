"""A unit test written in Python (standard library only), run by the pinned
host Python through the generic runner `python_test.c`.

The runner resolves every argument that names a runfile through the
runfiles manifest, so tests receive real paths of their data (scripts,
built binaries). The pinned interpreter is the one in
`//bazel/platforms:host_tools.bzl`; no Python rules or downloads are involved.
"""

load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("//bazel/platforms:host_tools.bzl", "HOST_PYTHON")

def host_python_test(
        name,
        main,
        data = [],
        args = [],
        size = "small",
        tags = ["local"],
        env = {},
        target_compatible_with = []):
    """Runs `python -B <main> <args...>` as a Bazel test.

    Args:
      name: test label.
      main: the test script (a label in this package or elsewhere).
      data: runfiles the script reads or starts; `main` is added.
      args: further arguments; `$(rootpath ...)` of a `data` entry resolves
        to its runfile path.
      size, tags, env, target_compatible_with: as for cc_test. Tests that
        read the original data (`ES2_DATA_ROOT`) list `@es2_data//:cd` (or
        `@es2_data//:reference_binaries`) in `data`, so their cached result
        is keyed on the CD's content (tools/reference-game/data_inputs.bzl);
        "no-cache" only for inputs that repository does not track.
    """
    cc_test(
        name = name,
        srcs = ["//bazel/defs:python_test.c"],
        data = [main] + data,
        args = [HOST_PYTHON, "-B", "$(rootpath %s)" % main] + args,
        size = size,
        tags = tags,
        env = env,
        target_compatible_with = target_compatible_with,
    )
