"""Own C toolchain configuration for the pinned Windows LLVM-MinGW SDK."""

load("@rules_cc//cc/common:cc_common.bzl", "cc_common")
load("@rules_cc//cc/toolchains:cc_toolchain_config_info.bzl", "CcToolchainConfigInfo")
load("@rules_cc//cc:action_names.bzl", "ACTION_NAMES")
load("@rules_cc//cc:cc_toolchain_config_lib.bzl", "action_config", "artifact_name_pattern", "feature", "flag_group", "flag_set", "tool", "variable_with_value")
load("//bazel/platforms:host_tools.bzl", "HOST_DEPENDENCIES")

_COMPILE = [ACTION_NAMES.c_compile, ACTION_NAMES.cpp_compile, ACTION_NAMES.linkstamp_compile, ACTION_NAMES.assemble, ACTION_NAMES.preprocess_assemble]
_LINK = [ACTION_NAMES.cpp_link_executable, ACTION_NAMES.cpp_link_dynamic_library, ACTION_NAMES.cpp_link_nodeps_dynamic_library]
_ARCHIVE = ACTION_NAMES.cpp_link_static_library

def _flags(name, actions, groups):
    return feature(name = name, enabled = True, flag_sets = [flag_set(actions = actions, flag_groups = groups)])

def _sequence(variable, flags):
    return flag_group(iterate_over = variable, expand_if_available = variable, flags = flags)

def _library(kind, flags):
    return flag_group(expand_if_equal = variable_with_value(name = "libraries_to_link.type", value = kind), flags = flags)

def _impl(ctx):
    # Bazel resolves these paths relative to the cc_toolchain package (this one).
    # Go from it to the exec root, then to declared SDK files.
    up = "../" * len(ctx.label.package.split("/"))
    compiler = up + ctx.file.compiler.path
    archiver = up + ctx.file.archiver.path
    actions = [action_config(action_name = a, enabled = True, tools = [tool(path = compiler)]) for a in _COMPILE + _LINK]
    actions += [action_config(action_name = _ARCHIVE, enabled = True, tools = [tool(path = archiver)])]
    actions += [action_config(action_name = ACTION_NAMES.strip, enabled = True, tools = [tool(path = up + ctx.file.strip.path)])]
    features = [
        feature(name = "no_legacy_features", enabled = True),
        feature(name = "targets_windows", enabled = True),
        feature(name = "supports_dynamic_linker", enabled = True),
        _flags("c17", [ACTION_NAMES.c_compile], [flag_group(flags = ["-std=c17"])]),
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
        _flags("shared", _LINK[1:], [flag_group(flags = ["-shared"])]),
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
                _library("dynamic_library", ["-l%{libraries_to_link.name}"]),
                _library("versioned_dynamic_library", ["%{libraries_to_link.name}"]),
            ],
        )]),
        # Reproducible PE files: no link time in the COFF header, so a relink of
        # unchanged objects gives identical bytes and Bazel keeps the dependent
        # test results cached.
        _flags("reproducible_link", _LINK, [flag_group(flags = ["-Wl,--no-insert-timestamp"])]),
        _flags("user_link_flags", _LINK, [_sequence("user_link_flags", ["%{user_link_flags}"])]),
        _flags("strip_flags", [ACTION_NAMES.strip], [flag_group(flags = ["-o", "%{output_file}", "%{input_file}"])]),
    ]
    root = HOST_DEPENDENCIES + "/llvm-mingw"
    return cc_common.create_cc_toolchain_config_info(
        ctx = ctx,
        toolchain_identifier = "llvm-mingw-20260922-" + ctx.attr.arch,
        host_system_name = "x86_64-w64-mingw32",
        target_system_name = ctx.attr.arch + "-w64-mingw32",
        target_cpu = "x64_windows" if ctx.attr.arch == "x86_64" else "x86_windows",
        target_libc = "ucrt",
        compiler = "clang",
        abi_version = "mingw",
        abi_libc_version = "ucrt",
        action_configs = actions,
        features = features,
        cxx_builtin_include_directories = [root + "/lib/clang/23/include", root + "/include", root + "/" + ctx.attr.arch + "-w64-mingw32/include"],
        artifact_name_patterns = [
            artifact_name_pattern(category_name = "executable", prefix = "", extension = ".exe"),
            artifact_name_pattern(category_name = "static_library", prefix = "lib", extension = ".a"),
            artifact_name_pattern(category_name = "dynamic_library", prefix = "", extension = ".dll"),
        ],
    )

llvm_mingw_config = rule(
    implementation = _impl,
    attrs = {
        "arch": attr.string(mandatory = True, values = ["i686", "x86_64"]),
        "compiler": attr.label(mandatory = True, allow_single_file = True),
        "archiver": attr.label(mandatory = True, allow_single_file = True),
        "strip": attr.label(mandatory = True, allow_single_file = True),
    },
    provides = [CcToolchainConfigInfo],
)
