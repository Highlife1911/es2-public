"""Expose the already-installed SDK as declared local inputs, without fetching."""

load("//bazel/platforms:host_repository.bzl", "dependencies_root")

def _llvm_mingw_repository(ctx):
    path = dependencies_root(ctx) + "/" + ctx.attr.path
    root = ctx.path(path)
    if not root.get_child("bin/clang.exe").exists:
        fail("The configured LLVM-MinGW installation is missing: " + path)
    ctx.symlink(root, "sdk")
    ctx.file("BUILD.bazel", """
package(default_visibility = ["//visibility:public"])
exports_files(glob(["sdk/bin/*"]))
filegroup(name = "tools", srcs = glob(["sdk/bin/*"]))
filegroup(name = "common_headers", srcs = glob(["sdk/include/**", "sdk/lib/clang/23/include/**"]))
filegroup(name = "i686_sdk", srcs = glob(["sdk/i686-w64-mingw32/**", "sdk/lib/clang/23/lib/windows/*i386*"]))
filegroup(name = "x86_64_sdk", srcs = glob(["sdk/x86_64-w64-mingw32/**", "sdk/lib/clang/23/lib/windows/*x86_64*"]))
""")

llvm_mingw_repository = repository_rule(
    implementation = _llvm_mingw_repository,
    attrs = {"path": attr.string(mandatory = True)},
    local = True,
)
