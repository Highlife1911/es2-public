"""Own C/C++ toolchain configuration for the pinned Emscripten SDK (wasm32-emscripten)."""

load("@rules_cc//cc/common:cc_common.bzl", "cc_common")
load("@rules_cc//cc/toolchains:cc_toolchain_config_info.bzl", "CcToolchainConfigInfo")
load("@rules_cc//cc:action_names.bzl", "ACTION_NAMES")
load("@rules_cc//cc:cc_toolchain_config_lib.bzl", "action_config", "artifact_name_pattern", "feature", "flag_group", "flag_set", "tool", "variable_with_value")

_C = [ACTION_NAMES.c_compile]
_CPP = [ACTION_NAMES.cpp_compile]
_COMPILE = [ACTION_NAMES.c_compile, ACTION_NAMES.cpp_compile, ACTION_NAMES.linkstamp_compile, ACTION_NAMES.assemble, ACTION_NAMES.preprocess_assemble]
_LINK = [ACTION_NAMES.cpp_link_executable]
_ARCHIVE = ACTION_NAMES.cpp_link_static_library

# Link options that give the guest-memory layout of docs/web_build.md section 5:
# static data from 0x00800000 (Emscripten 6 puts the stack after the static data
# and the heap after the stack by default), a 16 MiB C stack and a growable heap.
LINK_DEFAULTS = [
    "-O2",
    "-sGLOBAL_BASE=8388608",
    "-sSTACK_SIZE=16MB",
    "-sINITIAL_MEMORY=64MB",
    "-sALLOW_MEMORY_GROWTH",
    "-sMAXIMUM_MEMORY=2GB",
    "-sFILESYSTEM=0",
    "-sEXIT_RUNTIME",
    "-sENVIRONMENT=worker,node",
]

def _flags(name, actions, groups):
    return feature(name = name, enabled = True, flag_sets = [flag_set(actions = actions, flag_groups = groups)])

def _sequence(variable, flags):
    return flag_group(iterate_over = variable, expand_if_available = variable, flags = flags)

def _library(kind, flags):
    return flag_group(expand_if_equal = variable_with_value(name = "libraries_to_link.type", value = kind), flags = flags)

def _impl(ctx):
    if not ctx.attr.root:
        fail("The emsdk is not installed (the wasm toolchain is empty); run tools/setup-web-dependencies.ps1")

    # Bazel resolves these paths relative to the cc_toolchain package (this one).
    # Go from it to the exec root, then to the declared wrapper executables.
    up = "../" * len(ctx.label.package.split("/"))
    actions = [action_config(action_name = a, enabled = True, tools = [tool(path = up + ctx.file.emcc.path)]) for a in _C + [ACTION_NAMES.assemble, ACTION_NAMES.preprocess_assemble]]
    actions += [action_config(action_name = a, enabled = True, tools = [tool(path = up + ctx.file.empp.path)]) for a in _CPP + [ACTION_NAMES.linkstamp_compile] + _LINK]
    actions += [action_config(action_name = _ARCHIVE, enabled = True, tools = [tool(path = up + ctx.file.emar.path)])]

    # Bazel insists on a strip action for every binary; nothing requests the
    # stripped file of a wasm bundle, and emcc's own -O2 link output is minified.
    actions += [action_config(action_name = ACTION_NAMES.strip, enabled = True, tools = [tool(path = up + ctx.file.emar.path)])]
    features = [
        feature(name = "no_legacy_features", enabled = True),
        feature(name = "targets_wasm", enabled = True),
        _flags("c17", _C, [flag_group(flags = ["-std=c17"])]),
        _flags("c++17", _CPP + [ACTION_NAMES.linkstamp_compile], [flag_group(flags = ["-std=c++17"])]),
        _flags("compile_defaults", _COMPILE, [flag_group(flags = ["-Wall", "-Wextra", "-Werror", "-O2"])]),
        _flags("compile_inputs", _COMPILE, [flag_group(flags = ["-c", "%{source_file}", "-o", "%{output_file}"])]),
        _flags("dependency_file", _COMPILE, [flag_group(expand_if_available = "dependency_file", flags = ["-MD", "-MF", "%{dependency_file}"])]),
        _flags("include_paths", _COMPILE, [
            _sequence("quote_include_paths", ["-iquote", "%{quote_include_paths}"]),
            _sequence("include_paths", ["-I", "%{include_paths}"]),
            _sequence("system_include_paths", ["-isystem", "%{system_include_paths}"]),
        ]),
        _flags("preprocessor_defines", _COMPILE, [_sequence("preprocessor_defines", ["-D%{preprocessor_defines}"])]),
        _flags("user_compile_flags", _COMPILE, [_sequence("user_compile_flags", ["%{user_compile_flags}"])]),
        _flags("link_output", _LINK, [flag_group(flags = ["-o", "%{output_execpath}"])]),
        _flags("archive_output", [_ARCHIVE], [flag_group(flags = ["rcsD", "%{output_execpath}"])]),
        _flags("library_search_directories", _LINK, [_sequence("library_search_directories", ["-L%{library_search_directories}"])]),
        _flags("libraries_to_link", _LINK + [_ARCHIVE], [flag_group(
            iterate_over = "libraries_to_link",
            expand_if_available = "libraries_to_link",
            flag_groups = [
                _library("object_file", ["%{libraries_to_link.name}"]),
                flag_group(expand_if_equal = variable_with_value(name = "libraries_to_link.type", value = "object_file_group"), flag_groups = [
                    flag_group(iterate_over = "libraries_to_link.object_files", flags = ["%{libraries_to_link.object_files}"]),
                ]),
                _library("static_library", ["%{libraries_to_link.name}"]),
                _library("interface_library", ["%{libraries_to_link.name}"]),
            ],
        )]),
        _flags("web_link_defaults", _LINK, [flag_group(flags = LINK_DEFAULTS)]),
        _flags("user_link_flags", _LINK, [_sequence("user_link_flags", ["%{user_link_flags}"])]),
    ]
    root = ctx.attr.root
    return cc_common.create_cc_toolchain_config_info(
        ctx = ctx,
        toolchain_identifier = "emscripten-" + ctx.attr.version + "-wasm32",
        host_system_name = "x86_64-w64-mingw32",
        target_system_name = "wasm32-unknown-emscripten",
        target_cpu = "wasm32",
        target_libc = "emscripten",
        compiler = "emscripten",
        abi_version = "emscripten",
        abi_libc_version = "emscripten",
        action_configs = actions,
        features = features,
        cxx_builtin_include_directories = [
            root + "/upstream/lib/clang/" + ctx.attr.clang_version + "/include",
            root + "/upstream/emscripten/cache/sysroot/include",
        ],
        artifact_name_patterns = [
            # One file per link (wasm_bundle.h); wasm_binary unpacks the .js and .wasm.
            artifact_name_pattern(category_name = "executable", prefix = "", extension = ".wasm"),
            artifact_name_pattern(category_name = "static_library", prefix = "lib", extension = ".a"),
        ],
    )

def _exec_files_impl(ctx):
    return [DefaultInfo(files = depset(transitive = [target[DefaultInfo].files for target in ctx.attr.srcs]))]

exec_files = rule(
    implementation = _exec_files_impl,
    attrs = {"srcs": attr.label_list(allow_files = True, cfg = "exec")},
    doc = "The files of `srcs` built for the machine that runs the build, for the file lists of a cc_toolchain, which Bazel analyses in the target configuration.",
)

emscripten_config = rule(
    implementation = _impl,
    attrs = {
        "emcc": attr.label(mandatory = True, allow_single_file = True, executable = True, cfg = "exec"),
        "empp": attr.label(mandatory = True, allow_single_file = True, executable = True, cfg = "exec"),
        "emar": attr.label(mandatory = True, allow_single_file = True, executable = True, cfg = "exec"),
        "root": attr.string(mandatory = True),
        "version": attr.string(mandatory = True),
        "clang_version": attr.string(mandatory = True),
    },
    provides = [CcToolchainConfigInfo],
)
