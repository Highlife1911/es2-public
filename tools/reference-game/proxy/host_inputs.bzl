"""Declare existing host files for the explicitly local DirectDraw generator.

`python` is below the dependency directory, `ddraw` is absolute.
"""

load("//bazel/platforms:host_repository.bzl", "dependencies_root")

def _proxy_host_inputs_impl(ctx):
    for attribute, filename in [("python", "python.exe"), ("ddraw", "ddraw.dll")]:
        value = getattr(ctx.attr, attribute)
        path = ctx.path(dependencies_root(ctx) + "/" + value if attribute == "python" else value)
        if not path.exists:
            fail("Required existing proxy host input is missing: " + str(path))
        ctx.symlink(path, filename)
    ctx.file("BUILD.bazel", """
package(default_visibility = ["//visibility:public"])
exports_files(["python.exe", "ddraw.dll"])
""")

proxy_host_inputs = repository_rule(
    implementation = _proxy_host_inputs_impl,
    attrs = {
        "python": attr.string(mandatory = True),
        "ddraw": attr.string(mandatory = True),
    },
    local = True,
)
