"""Local OS-dependent export generation; no original game binaries are inputs."""

def _system_ddraw_exports_impl(ctx):
    header = ctx.actions.declare_file(ctx.label.name + "/exports.h")
    assembly = ctx.actions.declare_file(ctx.label.name + "/forward.S")
    definition = ctx.actions.declare_file(ctx.label.name + "/ddraw.def")
    ctx.actions.run(
        # Execute at the installed path so Python finds its unchanged runtime.
        # Its executable digest is declared; runtime/stdlib remain host inputs.
        executable = ctx.attr.python_path,
        arguments = [
            "-B", ctx.file.generator.path,
            "--readobj", ctx.file.readobj.path,
            "--dll", ctx.file.system_ddraw.path,
            "--out", header.dirname,
        ],
        inputs = [ctx.file.generator, ctx.file.python, ctx.file.system_ddraw],
        tools = ctx.files.readobj_runtime,
        outputs = [header, assembly, definition],
        mnemonic = "LocalDirectDrawExports",
        progress_message = "Reading declared host SysWOW64 DirectDraw exports (local, non-hermetic)",
        execution_requirements = {"local": "1", "no-remote": "1", "no-sandbox": "1"},
        use_default_shell_env = True,
    )
    return [
        DefaultInfo(files = depset([header, assembly, definition])),
        OutputGroupInfo(header = depset([header]), assembly = depset([assembly]), definition = depset([definition])),
    ]

system_ddraw_exports = rule(
    implementation = _system_ddraw_exports_impl,
    attrs = {
        "generator": attr.label(allow_single_file = [".py"], mandatory = True),
        "python": attr.label(allow_single_file = True, mandatory = True),
        "python_path": attr.string(mandatory = True),
        "readobj": attr.label(allow_single_file = True, mandatory = True),
        "readobj_runtime": attr.label(allow_files = True, mandatory = True),
        "system_ddraw": attr.label(allow_single_file = True, mandatory = True),
    },
)
