"""Generated #include lists derived from hook_registry.h (see hook_groups.py).

A new hook group adds its own *_hooks.h / *_cases.h file and its registry
lines; the include order follows the registry, so no package edits a shared
include list. Only the pinned host Python runs; no original data is read.
"""

load("//bazel/platforms:host_tools.bzl", "HOST_PYTHON")

def _hook_group_header_impl(ctx):
    out = ctx.actions.declare_file(ctx.label.name + ".h")
    args = ctx.actions.args()
    args.add_all(["-I", "-B", ctx.file._generator.path])
    args.add("--kind", ctx.attr.kind)
    args.add("--registry", ctx.file.registry.path)
    args.add("--prefix", ctx.attr.prefix)
    args.add_all(ctx.attr.exclude, before_each = "--exclude")
    args.add("--out", out.path)
    args.add_all(ctx.files.headers)
    ctx.actions.run(
        # The pinned Python runs at its installed path (as for the export
        # generator); its executable digest is a declared input.
        executable = HOST_PYTHON,
        arguments = [args],
        inputs = [ctx.file._generator, ctx.file._python, ctx.file.registry] + ctx.files.headers,
        outputs = [out],
        mnemonic = "HookGroupHeader",
        progress_message = "Deriving %{label} from the hook registry",
        execution_requirements = {"no-remote": "1"},
        use_default_shell_env = True,
    )
    return [DefaultInfo(files = depset([out]))]

hook_group_header = rule(
    implementation = _hook_group_header_impl,
    attrs = {
        "kind": attr.string(mandatory = True, values = ["adapters", "cases"]),
        "registry": attr.label(allow_single_file = [".h"], mandatory = True),
        "headers": attr.label_list(allow_files = [".h"], mandatory = True),
        "prefix": attr.string(mandatory = True),
        "exclude": attr.string_list(),
        "_generator": attr.label(default = Label("//tools/reference-game/proxy:hook_groups.py"), allow_single_file = [".py"]),
        "_python": attr.label(default = Label("@proxy_host//:python.exe"), allow_single_file = True),
    },
)
