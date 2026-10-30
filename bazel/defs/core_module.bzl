"""One simulation module of core/ and its unit test.

Every module follows the same layout: `src/<name>.c` (plus the private
little-endian helpers `src/fixed.h`), the public header
`include/es2/core/<name>.h`, and the unit test `tests/test_<name>.c`. The
macro spells that out once; `core/BUILD.bazel` lists only what differs
(dependencies, extra sources or headers, the guest-memory header, a test
whose executable name must avoid Windows' installer heuristic).

Object files are linked in `srcs` order and the proxy DLL's bytes depend on
the link order of `//core:core`, so a module keeps its sources in the order
given here and the aggregate list in `core/BUILD.bazel` stays explicit.
"""

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("//bazel/defs:wasm.bzl", "wasm_cc_test")

def core_test(name, src, deps):
    """A small cc_test over the given core libraries, and the same test built for wasm (`<name>_wasm`)."""
    cc_test(
        name = name,
        srcs = [src],
        deps = deps,
        size = "small",
    )
    wasm_cc_test(name = name + "_wasm", test = ":" + name)

def core_module(
        name,
        deps = [],
        extra_srcs = [],
        extra_hdrs = [],
        guest = False,
        test = True,
        test_name = None,
        test_src = None,
        test_deps = []):
    """Declares `//core:<name>` and, unless `test` is False, its unit test.

    Args:
      name: module name; names `src/<name>.c`, `include/es2/core/<name>.h`
        and `tests/test_<name>.c`.
      deps: other core modules the sources include.
      extra_srcs: further `src/*.c` files linked into this module, after
        `src/<name>.c`.
      extra_hdrs: further public headers under `include/es2/core/`.
      guest: True when the public header includes `guest.h` (guest memory
        and callback types).
      test: False for a module covered by another module's test.
      test_name: test label when `<name>_test` would trip Windows' installer
        heuristic ("update", "patch" in an executable name; error 740).
      test_src: the test source when it is not `tests/test_<name>.c`.
      test_deps: core modules the test links besides this one.
    """
    cc_library(
        name = name,
        srcs = ["src/%s.c" % name] + extra_srcs + ["src/fixed.h"],
        hdrs = ["include/es2/core/%s.h" % name] + extra_hdrs +
               (["include/es2/core/guest.h"] if guest else []),
        strip_include_prefix = "include",
        deps = deps,
        linkstatic = True,
    )
    if test:
        core_test(
            name = test_name or name + "_test",
            src = test_src or "tests/test_%s.c" % name,
            deps = [":" + name] + test_deps,
        )
