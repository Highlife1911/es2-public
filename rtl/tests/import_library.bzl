"""An i386 import library from a .def file (llvm-dlltool of the pinned LLVM-MinGW).

Only for the stand-in executable of //rtl:cw3220_delegate_test, which must import
names from cw3220.dll statically, as ES.EXE and VSHELL.EXE do.
"""

def _import_library_impl(ctx):
    out = ctx.actions.declare_file(ctx.attr.name + ".a")
    ctx.actions.run(
        executable = ctx.executable.dlltool,
        arguments = ["-m", "i386", "-d", ctx.file.definition.path, "-l", out.path],
        inputs = [ctx.file.definition],
        tools = ctx.files.runtime,
        outputs = [out],
        mnemonic = "ImportLibrary",
        progress_message = "Import library %s" % out.short_path,
    )
    return [DefaultInfo(files = depset([out]))]

import_library = rule(
    implementation = _import_library_impl,
    attrs = {
        "definition": attr.label(mandatory = True, allow_single_file = [".def"]),
        "dlltool": attr.label(
            default = "@llvm_mingw//:sdk/bin/llvm-dlltool.exe",
            allow_single_file = True,
            executable = True,
            cfg = "exec",
        ),
        "runtime": attr.label(default = "@llvm_mingw//:tools", cfg = "exec"),
    },
)
