"""@es2_data: the reference CD inputs of the real-data tests as tracked labels.

The real-data tests read the extracted CD at ES2_DATA_ROOT (a `--test_env` of
local.bazelrc). A path in the environment is part of a test's cache key, the bytes
behind it are not. This repository rule reads the same root from
`--repo_env=ES2_DATA_ROOT` (local.bazelrc sets both to the same path) and exposes:

* `@es2_data//:reference_binaries`: the key binaries of
  data/reference_hashes.txt (VER95/DBSIM.EXE, VER95/VSHELL.EXE, CW3220.DLL,
  ...), each verified against its pinned SHA-256 and copied into the
  repository (Bazel's output base, never the source tree), plus `status.txt`;
* `@es2_data//:cd`: the above plus `cd.sha256`, the SHA-256 of every CD file.

Tests list one of them in `data`, so their cached results are keyed on the
content and they rerun only when the data or the test changes. The tests keep
reading ES2_DATA_ROOT; the labels only carry the digests.

Without data (ES2_DATA_ROOT unset or empty, the path missing, or no
VER95/DBSIM.EXE under it or under its extracted/cd) the rule never fails: both
groups contain only a status file and an empty manifest that say so, and the
tests skip as before. A pinned binary with another hash fails the fetch.
The rule watches the CD tree, the table and the helper, so a change of any of
them refetches it.
"""

load("//bazel/platforms:host_tools.bzl", "HOST_PYTHON")

_LAYOUTS = ["", "extracted/cd"]
_PROBE = "VER95/DBSIM.EXE"

def _write_build(ctx, binaries):
    ctx.file("BUILD.bazel", """\
package(default_visibility = ["//visibility:public"])

exports_files({files})

filegroup(
    name = "reference_binaries",
    srcs = {binaries},
)

filegroup(
    name = "cd",
    srcs = [":reference_binaries", "cd.sha256"],
)
""".format(
        files = repr(["status.txt", "cd.sha256"] + binaries),
        binaries = repr(["status.txt"] + binaries),
    ))

def _absent(ctx, reason):
    ctx.file("status.txt", "absent: %s\n" % reason)
    ctx.file("cd.sha256", "# no reference CD: %s\n" % reason)
    _write_build(ctx, [])

def _es2_data_inputs_impl(ctx):
    # data/ is not a package; reach the pinned table from the module root.
    table = ctx.path(Label("//:MODULE.bazel")).dirname.get_child("data", "reference_hashes.txt")
    helper = ctx.path(Label("//tools/reference-game:data_inputs.py"))
    ctx.watch(table)
    ctx.watch(helper)
    root = ctx.getenv("ES2_DATA_ROOT", "").strip()
    if not root:
        _absent(ctx, "ES2_DATA_ROOT is unset or empty")
        return
    cd = None
    for layout in _LAYOUTS:
        candidate = ctx.path(root + ("/" + layout if layout else ""))
        probe = ctx.path(str(candidate) + "/" + _PROBE)
        ctx.watch(probe)
        if probe.exists:
            cd = candidate
            break
    if cd == None:
        _absent(ctx, "no %s below %s or its extracted/cd" % (_PROBE, root))
        return
    ctx.watch_tree(cd)
    python = ctx.path(HOST_PYTHON)
    if not python.exists:
        fail("@es2_data needs the pinned host Python " + HOST_PYTHON)
    result = ctx.execute([python, "-I", "-B", helper, cd, table, ctx.path(".")], timeout = 900, quiet = True)
    if result.return_code != 0:
        fail(("@es2_data: the reference files at %s do not match data/reference_hashes.txt " +
              "(or could not be read). Point --repo_env/--test_env ES2_DATA_ROOT at the extracted " +
              "reference CD, or set both empty to skip the real-data tests.\n%s%s") %
             (cd, result.stdout, result.stderr))
    binaries = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    ctx.file("status.txt", "present: %s\npinned binaries: %s\n" % (cd, " ".join(binaries)))
    _write_build(ctx, binaries)

es2_data_inputs = repository_rule(
    implementation = _es2_data_inputs_impl,
    doc = "Reference CD inputs of the real-data tests, content-tracked (see the file docstring).",
)

# The recorded suite outputs the shell's real-data tests read besides the CD:
# the goldens and the sealed launch sets of the es2-data root.
_RECORDED_TREES = ["oracle/golden", "oracle/launch"]

def _es2_recorded_inputs_impl(ctx):
    """@es2_recorded//:outputs: recorded.sha256 over _RECORDED_TREES.

    The data root is the es2-data checkout ES2_DATA_REPO; without it, the
    directory that holds extracted/cd: ES2_DATA_ROOT itself or two levels
    above it. Without either the file says so and the tests skip.
    """
    ctx.file("BUILD.bazel", """\
package(default_visibility = ["//visibility:public"])

filegroup(
    name = "outputs",
    srcs = ["recorded.sha256"],
)
""")
    helper = ctx.path(Label("//tools/reference-game:data_inputs.py"))
    ctx.watch(helper)
    repo = ctx.getenv("ES2_DATA_REPO", "").strip()
    root = ctx.getenv("ES2_DATA_ROOT", "").strip()
    data_root = None
    if repo:
        data_root = ctx.path(repo)
    elif root:
        for layout, up in [("extracted/cd", 0), ("", 2)]:
            candidate = ctx.path(root + ("/" + layout if layout else ""))
            probe = ctx.path(str(candidate) + "/" + _PROBE)
            ctx.watch(probe)
            if probe.exists:
                data_root = ctx.path(root)
                for _ in range(up):
                    data_root = data_root.dirname
                break
    if data_root == None:
        ctx.file("recorded.sha256", "# no es2-data root: ES2_DATA_REPO unset, ES2_DATA_ROOT unset or without %s\n" % _PROBE)
        return
    for tree in _RECORDED_TREES:
        path = data_root.get_child(*tree.split("/"))
        ctx.watch(path)
        if path.exists:
            ctx.watch_tree(path)
    python = ctx.path(HOST_PYTHON)
    if not python.exists:
        fail("@es2_recorded needs the pinned host Python " + HOST_PYTHON)
    result = ctx.execute([python, "-I", "-B", helper, "--recorded", data_root, ctx.path(".")] + _RECORDED_TREES, timeout = 900, quiet = True)
    if result.return_code != 0:
        fail("@es2_recorded: hashing the recorded outputs under %s failed\n%s%s" % (data_root, result.stdout, result.stderr))

es2_recorded_inputs = repository_rule(
    implementation = _es2_recorded_inputs_impl,
    doc = "The recorded suite outputs (oracle/golden, oracle/launch) of the es2-data root, content-tracked.",
)
