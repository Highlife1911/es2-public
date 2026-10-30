"""Expose the already-installed emsdk to Bazel without fetching anything.

The SDK (about 2.7 GB with its LLVM, Node and Python) is not declared file by
file. The repository holds the absolute paths of the SDK's parts (`paths.bzl`)
and the install stamp `es2-install.json`, which records the pinned archive
checksums and the frozen system libraries (tools/setup-web-dependencies.ps1).
The toolchain's actions list the stamp as an input, so a different SDK changes
every action key. `path` is below the dependency directory.

Every build resolves the registered toolchains, so a machine without the web
dependencies must still load this repository: it then holds empty paths and
the wasm toolchain fails when a wasm target needs it.
"""

load("//bazel/platforms:host_repository.bzl", "dependencies_root")

def _single_child(ctx, directory, file):
    """`file` in the one version directory below `directory` (emsdk installs `python/<version>`)."""
    names = [child.basename for child in ctx.path(directory).readdir()]
    if len(names) != 1:
        fail("Expected exactly one version below %s, found %s" % (directory, names))
    path = "%s/%s/%s" % (directory, names[0], file)
    if not ctx.path(path).exists:
        fail("The emsdk installation is missing " + path)
    return path

def _emscripten_repository(ctx):
    root = dependencies_root(ctx) + "/" + ctx.attr.path
    ctx.file("BUILD.bazel", """
package(default_visibility = ["//visibility:public"])
exports_files(["paths.bzl"])
filegroup(name = "stamp", srcs = glob(["es2-install.json", "emscripten_config"]))
""")
    if not ctx.path(root + "/es2-install.json").exists:
        ctx.file("paths.bzl", 'EMSDK_ROOT = ""\nEMSDK_PYTHON = ""\nEMSDK_NODE = ""\n')
        return
    for required in ["upstream/emscripten/emcc.py", "upstream/emscripten/em++.py", "upstream/emscripten/emar.py", ".emscripten"]:
        if not ctx.path(root + "/" + required).exists:
            fail("The configured emsdk installation is incomplete (missing %s): %s" % (required, root))
    stamp = ctx.read(root + "/es2-install.json")
    if not json.decode(stamp).get("frozenLibs"):
        fail("The emsdk has no frozen system libraries; run tools/setup-web-dependencies.ps1: " + root)
    ctx.file("es2-install.json", stamp)
    ctx.file("emscripten_config", ctx.read(root + "/.emscripten"))
    ctx.file("paths.bzl", """EMSDK_ROOT = "%s"
EMSDK_PYTHON = "%s"
EMSDK_NODE = "%s"
""" % (root, _single_child(ctx, root + "/python", "python.exe"), _single_child(ctx, root + "/node", "node.exe")))

emscripten_repository = repository_rule(
    implementation = _emscripten_repository,
    attrs = {"path": attr.string(mandatory = True)},
    local = True,
)
