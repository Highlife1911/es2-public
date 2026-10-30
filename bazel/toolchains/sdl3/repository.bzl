"""Declare the approved prebuilt SDL3 Windows SDK as ordinary Bazel inputs."""

load("//bazel/platforms:host_repository.bzl", "dependencies_root")

def _sdl3_repository_impl(ctx):
    installation = ctx.path(dependencies_root(ctx) + "/" + ctx.attr.path)
    if not installation.get_child("i686-w64-mingw32/include/SDL3/SDL.h").exists:
        fail("Install the pinned SDL3 SDK with tools/setup-dependencies.ps1 -Platform")
    ctx.symlink(installation, "sdk")
    # cc_import's Windows interface slot accepts .lib, not MinGW's .dll.a
    # spelling. These are aliases to the unchanged COFF import archives.
    for arch in ["i686", "x86_64"]:
        ctx.symlink(installation.get_child(arch + "-w64-mingw32/lib/libSDL3.dll.a"), arch + "/SDL3.lib")
    ctx.file("BUILD.bazel", """
load("@rules_cc//cc:cc_import.bzl", "cc_import")
package(default_visibility = ["//visibility:public"])
config_setting(name = "x86", constraint_values = ["@platforms//cpu:x86_32"])
[
    cc_import(
        name = arch,
        hdrs = glob(["sdk/" + arch + "-w64-mingw32/include/SDL3/**"]),
        includes = ["sdk/" + arch + "-w64-mingw32/include"],
        interface_library = arch + "/SDL3.lib",
        shared_library = "sdk/" + arch + "-w64-mingw32/bin/SDL3.dll",
    )
    for arch in ["i686", "x86_64"]
]
alias(name = "sdl3", actual = select({":x86": ":i686", "//conditions:default": ":x86_64"}))
filegroup(
    name = "runtime",
    srcs = select({
        ":x86": ["sdk/i686-w64-mingw32/bin/SDL3.dll"],
        "//conditions:default": ["sdk/x86_64-w64-mingw32/bin/SDL3.dll"],
    }),
)
""")

sdl3_repository = repository_rule(
    implementation = _sdl3_repository_impl,
    attrs = {
        # Below the dependency directory.
        "path": attr.string(mandatory = True),
    },
    local = True,
)
