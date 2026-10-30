"""The machine's dependency directory and host tools as a repository (@es2_host).

The pinned tools live in the dependency directory of docs/dependencies.md:
`--repo_env=ES2_DEPENDENCIES` (written to local.bazelrc by
tools/write-local-bazelrc.ps1), else `es2-dependencies` beside the checkout.
"""

def dependencies_root(ctx):
    """The dependency directory as an absolute path with forward slashes."""
    root = ctx.getenv("ES2_DEPENDENCIES", "").strip()
    if not root:
        root = str(ctx.workspace_root.dirname) + "/es2-dependencies"
    return root.replace("\\", "/").rstrip("/")

def _es2_host_impl(ctx):
    pwsh = ctx.getenv("ES2_PWSH", "").strip() or ctx.which("pwsh.exe") or "pwsh.exe"
    ctx.file("BUILD.bazel", 'exports_files(["paths.bzl"])\n')
    ctx.file("paths.bzl", 'DEPENDENCIES = "%s"\nPWSH = "%s"\n' % (dependencies_root(ctx), str(pwsh).replace("\\", "/")))

es2_host = repository_rule(
    implementation = _es2_host_impl,
    local = True,
)
