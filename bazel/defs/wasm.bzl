"""Building for wasm32-emscripten and running the result under Node.

`wasm_binary` builds a `cc_binary` (or `cc_test`) for the wasm platform and
unpacks its link output into `<name>.js` and `<name>.wasm`. `node_test` runs
a JS file under the emsdk's Node, as `host_python_test` runs a script under
the pinned Python. `wasm_cc_test` combines both for an existing portable
`cc_test`: the same sources and dependencies, built with the toolchain in
`//bazel/toolchains/emscripten`.

The libraries and the test come from the same BUILD targets as the Windows
build, in a second configuration (the platform transition), so the Windows
outputs, action keys and cached results stay as they are.
"""

load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("//bazel/platforms:host_tools.bzl", "HOST_NODE")

def _wasm_transition_impl(settings, attr):
    return {
        "//command_line_option:platforms": [str(Label("//bazel/platforms:wasm32_emscripten"))],
        "//command_line_option:linkopt": settings["//command_line_option:linkopt"] + attr.linkopts,
    }

_wasm_transition = transition(
    implementation = _wasm_transition_impl,
    inputs = ["//command_line_option:linkopt"],
    outputs = ["//command_line_option:platforms", "//command_line_option:linkopt"],
)

def _wasm_binary_impl(ctx):
    binary = ctx.attr.binary[0]  # a transitioned attribute is a list
    bundles = [f for f in binary[DefaultInfo].files.to_list() if f.basename.endswith(".wasm")]
    if len(bundles) != 1:
        fail("%s is not a binary linked with the wasm toolchain" % binary.label)
    bundle = bundles[0]

    # The glue code names the .wasm file after the link output, so both keep the binary's name.
    base = bundle.basename[:-len(".wasm")]
    js = ctx.actions.declare_file(base + ".js")
    wasm = ctx.actions.declare_file(base + ".wasm")
    ctx.actions.run(
        executable = ctx.executable._unpack,
        arguments = [bundle.path, base + ".js", js.path, base + ".wasm", wasm.path],
        inputs = [bundle],
        outputs = [js, wasm],
        mnemonic = "WasmUnpack",
        progress_message = "Unpacking %{label}",
    )
    return [
        DefaultInfo(files = depset([js, wasm]), runfiles = ctx.runfiles(files = [js, wasm])),
        OutputGroupInfo(js = depset([js]), wasm = depset([wasm])),
    ]

wasm_binary = rule(
    implementation = _wasm_binary_impl,
    attrs = {
        "binary": attr.label(
            mandatory = True,
            cfg = _wasm_transition,
            doc = "A cc_binary or cc_test, built for wasm32-emscripten. The outputs are named after it.",
        ),
        "linkopts": attr.string_list(doc = "Link options added for this build of `binary` only (a further configuration)."),
        "_unpack": attr.label(
            default = "//bazel/toolchains/emscripten:wasm_unpack",
            executable = True,
            cfg = "exec",
        ),
    },
    doc = "The `.js` and `.wasm` outputs (output groups `js` and `wasm`) of a binary built with the Emscripten toolchain.",
)

def node_test(
        name,
        main,
        data = [],
        args = [],
        size = "small",
        tags = ["local"],
        env = {},
        target_compatible_with = []):
    """Runs `node <main> <args...>` as a Bazel test through the generic runner.

    The runner `//bazel/defs:python_test.c` resolves every argument that names a runfile, so `main`
    and the files it loads (a `.wasm` next to it) are real paths at run time.

    Args:
      name: test label.
      main: the JS file (a label of one file).
      data: runfiles the script reads or loads.
      args: further arguments; `$(rootpath ...)` of a `data` entry resolves to its runfile path.
      size, tags, env, target_compatible_with: as for cc_test.
    """
    cc_test(
        name = name,
        srcs = ["//bazel/defs:python_test.c"],
        data = [main] + data,
        # The runner resolves runfiles from the third argument on, so a Node option precedes the script.
        args = [HOST_NODE, "--trace-uncaught", "$(rootpath %s)" % main] + args,
        size = size,
        tags = tags,
        env = env,
        target_compatible_with = target_compatible_with,
    )

def wasm_cc_test(
        name,
        test,
        data = [],
        args = [],
        size = "small",
        tags = ["local"],
        env = {},
        noderawfs = False):
    """Runs an existing portable `cc_test` built for wasm under Node.

    Args:
      name: test label (by convention `<test>_wasm`).
      test: the `cc_test` (or `cc_binary`) whose sources and dependencies are built for wasm.
      data: runfiles the test reads; repeat the `data` of `test`.
      args: arguments of the test program.
      size, tags, env: as for cc_test; repeat those of `test` where they matter.
      noderawfs: True for a test that reads files: it links with `-sFILESYSTEM=1 -sNODERAWFS`, so
        `fopen` reaches the host file system (a further configuration of the libraries).
    """
    wasm_binary(
        name = name + "_module",
        binary = test,
        linkopts = ["-sFILESYSTEM=1", "-sNODERAWFS"] if noderawfs else [],
        testonly = True,
    )
    native.filegroup(
        name = name + "_js",
        srcs = [":" + name + "_module"],
        output_group = "js",
        testonly = True,
    )
    node_test(
        name = name,
        main = ":" + name + "_js",
        data = [":" + name + "_module"] + data,
        args = args,
        size = size,
        tags = tags,
        env = env,
    )
