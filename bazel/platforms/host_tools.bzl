"""Host tools that Bazel actions call by absolute path.

Pinned in docs/dependencies.md and verified by `tools/setup-dependencies.ps1
-Python`; DEPENDENCIES is the dependency directory of @es2_host
(bazel/platforms/host_repository.bzl).
"""

load("@es2_host//:paths.bzl", "DEPENDENCIES")

HOST_DEPENDENCIES = DEPENDENCIES

HOST_PYTHON = DEPENDENCIES + "/python/python.exe"
