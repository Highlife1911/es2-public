"""Host tools that Bazel actions and tests call by absolute path.

Pinned in docs/dependencies.md and verified by `tools/setup-dependencies.ps1
-Python`; DEPENDENCIES is the dependency directory of @es2_host
(bazel/platforms/host_repository.bzl).
"""

load("@es2_host//:paths.bzl", "DEPENDENCIES", "PWSH")

HOST_DEPENDENCIES = DEPENDENCIES

HOST_PYTHON = DEPENDENCIES + "/python/python.exe"

# The emsdk's Node (docs/web_build.md section 14); runs the wasm tests. The emscripten toolchain checks that it is
# the Node of the configured emsdk.
HOST_NODE = DEPENDENCIES + "/emsdk/node/24.19.0_64bit/node.exe"

HOST_FFMPEG = DEPENDENCIES + "/ffmpeg/bin/ffmpeg.exe"

# System PowerShell 7 (ES2_PWSH, else pwsh.exe on PATH).
HOST_PWSH = PWSH
