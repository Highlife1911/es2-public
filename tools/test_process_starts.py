"""Window lint: every process start in the repository's own sources must not open a console window.

A child that is a console program, started by a process that has no console window of its own (a GUI
program, a scheduled task, a hidden launcher), gets a console window of its own: the pop-ups that
interrupt tests and oracle runs. This test lists every process start it can see in the
tracked *.ps1, *.psm1, *.py, *.c, *.h, *.cmd and *.bat files and requires, for each, one of

  * a flag that suppresses the window:
      C         CREATE_NO_WINDOW or DETACHED_PROCESS in the call or the 8 lines before it,
                STARTF_USESHOWWINDOW together with SW_HIDE (ShellExecute: SW_HIDE in the call)
      PowerShell Start-Process with -WindowStyle Hidden (also through a splatted hashtable that sets
                WindowStyle='Hidden') or -NoNewWindow (the child shares the caller's console and opens none);
                [Diagnostics.ProcessStartInfo] with CreateNoWindow = $true within 15 lines
      Python    subprocess / asyncio calls with creationflags= containing NO_WINDOW, CREATE_NO_WINDOW,
                DETACHED_PROCESS or 0x08000000
      cmd       start /B
  * an allow comment with a reason on the line of the call, the line above, or anywhere on the lines of a
    multi-line call:   # window-lint: allow <reason>     /* window-lint: allow <reason> */
    rem window-lint: allow <reason>
    The reason names the rule that makes the start safe: "gui-child <exe>" (the child is a GUI-subsystem
    program such as ES.EXE, VSHELL.EXE, DBSIM.EXE or the -mwindows launcher: no console is created),
    "inherits console" (the child is a console program but the parent owns a console, hidden or not, which it
    inherits; Bazel gives every test one, measured), "visible on purpose" (interactive start).
  * a file-level comment in the first 40 lines, `window-lint: allow-file <reason>`, for a file all of whose
    starts have the same reason (for example a test file that runs only under Bazel).

An allow comment that no longer sits at a process start is an error (stale marker), so the markers cannot
rot. The PowerShell call operator (`& program`) is not listed: whether it opens a window depends only on the
caller (a hidden or visible console, a GUI parent), which a text scan cannot know; scripts run in the
console of their caller, and every script that starts a child on its own account uses one of the calls above.

Run:  python -B tools/test_process_starts.py [--list]      (Bazel: //tools:process_starts_test)
"""
import io
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tokenize

ROOT = Path(__file__).resolve().parent.parent
SELF = "tools/test_process_starts.py"
EXTENSIONS = {".ps1", ".psm1", ".py", ".c", ".h", ".cmd", ".bat"}
SKIP_TOP = ("bazel-", "build/", ".git/", "es2-data/")

ALLOW = re.compile(r"window-lint:\s*(allow-file|allow)\b[\s:,\-]*(.*)")
PY_FLAG = re.compile(r"\b(?:NO_WINDOW|CREATE_NO_WINDOW|DETACHED_PROCESS)\b|0x08000000")


class Hit:
    def __init__(self, path, line, kind, text, status, detail=""):
        self.path, self.line, self.kind, self.text, self.status, self.detail = path, line, kind, text, status, detail

    def row(self):
        return "%s:%d  %-22s %-5s %s  [%s]" % (self.path, self.line, self.kind, self.status, self.detail, self.text)


# ---------------------------------------------------------------- comment / string blanking

def _blank(chars, a, b):
    for i in range(a, b):
        if chars[i] not in "\r\n":
            chars[i] = " "


def c_views(text):
    """(code, no_comments): comments blanked; code additionally blanks string and character literals."""
    code, nocom = list(text), list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            _blank(code, i, j); _blank(nocom, i, j); i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            _blank(code, i, j); _blank(nocom, i, j); i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            _blank(code, i + 1, min(j, n)); i = j + 1
        else:
            i += 1
    return "".join(code), "".join(nocom)


def ps_views(text):
    """PowerShell: comments blanked (string-aware); strings kept (scripts embed code in here-strings)."""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("<#", i):
            j = text.find("#>", i + 2)
            j = n if j < 0 else j + 2
            _blank(out, i, j); i = j
        elif c == "#":
            j = text.find("\n", i)
            j = n if j < 0 else j
            _blank(out, i, j); i = j
        elif c == "@" and i + 1 < n and text[i + 1] in "'\"" and re.match(r"[ \t]*\r?\n", text[i + 2:i + 12]):
            end = re.compile(r"\n" + re.escape(text[i + 1]) + "@").search(text, i + 2)
            i = n if not end else end.end()
        elif c in "'\"":
            j = i + 1
            while j < n:
                if c == '"' and text[j] == "`":
                    j += 2
                    continue
                if text[j] == c:
                    if j + 1 < n and text[j + 1] == c:
                        j += 2
                        continue
                    break
                j += 1
            i = j + 1
        elif c == "`":
            i += 2
        else:
            i += 1
    return "".join(out)


def py_views(text):
    """(code, no_comments, no_docstrings): code blanks comments and string contents, no_comments only the
    comments, no_docstrings also the string statements (docstrings)."""
    code, nocom = list(text), list(text)
    starts = [0]
    for m in re.finditer(r"\n", text):
        starts.append(m.end())

    def off(pos):
        return starts[min(pos[0], len(starts)) - 1] + pos[1]
    nodoc = list(text)
    try:
        previous = "NEWLINE"      # the last significant token kind; a docstring follows NEWLINE/INDENT/DEDENT
        pending = None
        for tok in tokenize.generate_tokens(io.StringIO(text).readline):
            kind = tokenize.tok_name.get(tok.type, "")
            a, b = off(tok.start), off(tok.end)
            if kind == "COMMENT":
                _blank(code, a, b); _blank(nocom, a, b); _blank(nodoc, a, b)
            elif kind in ("STRING", "FSTRING_MIDDLE"):
                _blank(code, a, b)
            if kind == "STRING" and previous in ("NEWLINE", "INDENT", "DEDENT"):
                pending = (a, b)
            elif kind == "NEWLINE" and pending and previous == "STRING":
                _blank(nodoc, *pending)       # a string statement: docstring, not code
                pending = None
            elif kind not in ("NL", "COMMENT"):
                pending = pending if kind == "NEWLINE" else None
            if kind not in ("NL", "COMMENT"):
                previous = kind
    except (tokenize.TokenError, IndentationError, SyntaxError):
        pass
    return "".join(code), "".join(nocom), "".join(nodoc)


# ---------------------------------------------------------------- helpers

def line_of(text, pos):
    return text.count("\n", 0, pos) + 1


def balanced_end(code, open_pos):
    """Index after the paren matching code[open_pos] == '('."""
    depth = 0
    for i in range(open_pos, len(code)):
        if code[i] == "(":
            depth += 1
        elif code[i] == ")":
            depth -= 1
            if depth == 0:
                return i + 1
    return len(code)


def ps_statement_end(code, pos):
    """End of the PowerShell statement that starts at pos: ends at a newline outside brackets that is not
    escaped by a backtick, or at ';'."""
    depth = 0
    i, n = pos, len(code)
    while i < n:
        c = code[i]
        if c == "`":
            i += 2
            continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == ";" and depth <= 0:
            return i
        elif c == "\n" and depth <= 0:
            return i
        i += 1
    return n


def snippet(text, a, b):
    return re.sub(r"\s+", " ", text[a:min(b, a + 90)]).strip()


def lines_around(text, a, b, before, after=0):
    la, lb = line_of(text, a), line_of(text, max(a, b - 1))
    lines = text.split("\n")
    lo, hi = max(1, la - before), min(len(lines), lb + after)
    return "\n".join(lines[lo - 1:hi])


# ---------------------------------------------------------------- scanners

C_CALL = re.compile(r"(?<![\w.])(?:CreateProcess(?:[AW]|AsUser[AW]|WithLogonW|WithTokenW)?|_?w?spawn[lv][pe]*|_?w?exec[lv][pe]*|"
                    r"ShellExecute(?:Ex)?[AW]?|WinExec|_?w?system|_?w?popen)\s*\(")


def scan_c(text):
    code, _ = c_views(text)
    for m in C_CALL.finditer(code):
        end = balanced_end(code, m.end() - 1)
        call = code[m.start():end]
        near = lines_around(code, m.start(), end, 8)
        name = m.group(0).rstrip("( \t")
        if re.search(r"\b(?:CREATE_NO_WINDOW|DETACHED_PROCESS)\b", near):
            yield m.start(), name, "flag", "CREATE_NO_WINDOW/DETACHED_PROCESS", snippet(code, m.start(), end)
        elif name.startswith("ShellExecute") and "SW_HIDE" in call:
            yield m.start(), name, "flag", "SW_HIDE", snippet(code, m.start(), end)
        elif "STARTF_USESHOWWINDOW" in lines_around(code, m.start(), end, 15) and "SW_HIDE" in lines_around(code, m.start(), end, 15):
            yield m.start(), name, "flag", "STARTF_USESHOWWINDOW+SW_HIDE", snippet(code, m.start(), end)
        else:
            yield m.start(), name, "none", "", snippet(code, m.start(), end)


PY_CALL = re.compile(r"(?<![\w])(?:subprocess\.(?:run|Popen|call|check_call|check_output|getoutput|getstatusoutput)|"
                     r"os\.(?:system|popen|startfile|spawn[lv][pe]*|exec[lv][pe]*|posix_spawnp?)|"
                     r"asyncio\.create_subprocess_(?:exec|shell))\s*\(|"
                     r"(?<![\w.])(?:Popen|check_output|check_call)\s*\(")


def scan_py(text):
    code, nocom, nodoc = py_views(text)
    for m in PY_CALL.finditer(code):
        end = balanced_end(code, m.end() - 1)
        call = code[m.start():end]
        name = m.group(0).rstrip("( \t")
        kw = re.search(r"\bcreationflags\s*=\s*([^,)]*(?:\([^)]*\))?[^,)]*)", call)
        if kw and PY_FLAG.search(kw.group(1)):
            yield m.start(), name, "flag", "creationflags", snippet(code, m.start(), end)
        elif re.search(r"\bstartupinfo\b", call) and "SW_HIDE" in lines_around(code, m.start(), end, 10):
            yield m.start(), name, "flag", "startupinfo SW_HIDE", snippet(code, m.start(), end)
        else:
            yield m.start(), name, "none", "", snippet(code, m.start(), end)
    # PowerShell embedded in Python strings (the script tests build small scripts); not docstrings
    for hit in scan_ps(nodoc, embedded=True):
        yield hit


PS_START_PROCESS = re.compile(r"(?<![\w-])Start-Process\b")
PS_PSI = re.compile(r"\bProcessStartInfo\b")
PS_PROCESS_START = re.compile(r"\bDiagnostics\.Process\]::Start\(\s*['\"]")


def scan_ps(text, embedded=False):
    code = text if embedded else ps_views(text)
    for m in PS_START_PROCESS.finditer(code):
        before = code[max(0, m.start() - 12):m.start()]
        if re.search(r"function\s+$", before):   # a mock definition, not a start
            continue
        end = ps_statement_end(code, m.end())
        stmt = code[m.start():end]
        splat = re.search(r"@(\w+)", stmt)
        if re.search(r"-WindowStyle[\s:]+['\"]?Hidden\b", stmt):
            yield m.start(), "Start-Process", "flag", "-WindowStyle Hidden", snippet(code, m.start(), end)
        elif re.search(r"-NoNewWindow\b", stmt):
            yield m.start(), "Start-Process", "flag", "-NoNewWindow (shares the caller's console)", snippet(code, m.start(), end)
        elif splat and re.search(r"\$" + splat.group(1) + r"\s*=\s*@\{[^}]*\bWindowStyle\s*=\s*['\"]?Hidden\b", code[:m.start()]):
            yield m.start(), "Start-Process", "flag", "splat WindowStyle='Hidden'", snippet(code, m.start(), end)
        else:
            yield m.start(), "Start-Process", "none", "", snippet(code, m.start(), end)
    for m in PS_PSI.finditer(code):
        end = ps_statement_end(code, m.end())
        near = lines_around(code, m.start(), end, 3, 15)
        if re.search(r"\bCreateNoWindow\s*=\s*\$true\b", near):
            yield m.start(), "ProcessStartInfo", "flag", "CreateNoWindow=$true", snippet(code, m.start(), end)
        else:
            yield m.start(), "ProcessStartInfo", "none", "", snippet(code, m.start(), end)
    for m in PS_PROCESS_START.finditer(code):
        end = ps_statement_end(code, m.end())
        yield m.start(), "Process::Start(file)", "none", "", snippet(code, m.start(), end)


CMD_START = re.compile(r"^[ \t]*@?start\b(?P<rest>.*)$", re.I | re.M)


def scan_cmd(text):
    body = re.sub(r"(?im)^[ \t]*(?:@?rem\b|::).*$", lambda m: " " * len(m.group(0)), text)
    for m in CMD_START.finditer(body):
        if re.search(r"/b\b", m.group("rest"), re.I):
            yield m.start(), "start", "flag", "start /B", snippet(body, m.start(), m.end())
        else:
            yield m.start(), "start", "none", "", snippet(body, m.start(), m.end())


def scan_text(path, text):
    """All process starts of one file as Hit objects with their disposition."""
    ext = os.path.splitext(path)[1].lower()
    scanner = {".c": scan_c, ".h": scan_c, ".py": scan_py, ".ps1": scan_ps, ".psm1": scan_ps,
               ".cmd": scan_cmd, ".bat": scan_cmd}[ext]
    raw = text.replace("\r\n", "\n")
    lines = raw.split("\n")
    markers = {}          # line -> (kind, reason)
    errors = []
    for number, line in enumerate(lines, 1):
        m = ALLOW.search(line)
        if m:
            reason = m.group(2).replace("*/", "").strip(" \t*")
            if len(reason) < 8:
                errors.append(Hit(path, number, "allow comment", line.strip()[:80], "FAIL", "reason missing or shorter than 8 characters"))
            markers[number] = (m.group(1), reason)
    file_marker = next(((n, r) for n, (k, r) in markers.items() if k == "allow-file" and n <= 40), None)
    for n, (k, r) in markers.items():
        if k == "allow-file" and n > 40:
            errors.append(Hit(path, n, "allow comment", "allow-file", "FAIL", "allow-file belongs into the first 40 lines"))
    used = set()
    hits = []
    for pos, kind, state, detail, text_snippet in scanner(raw):
        first = line_of(raw, pos)
        last = first
        # lines of the whole statement: from the hit to the end of its snippet is enough for the comment search
        found = None
        for ln in range(first - 1, first + 1):
            if ln in markers and markers[ln][0] == "allow":
                found = ln
        if found is None:
            # multi-line call: a marker on one of the lines between the hit and the closing line
            stmt_lines = raw[pos:].split("\n")
            depth = 0
            for offset, text_line in enumerate(stmt_lines[:40]):
                depth += text_line.count("(") - text_line.count(")")
                ln = first + offset
                if offset and ln in markers and markers[ln][0] == "allow":
                    found = ln
                    break
                if depth <= 0 and not text_line.rstrip().endswith(("`", ",", "\\")):
                    break
        if found is not None:
            used.add(found)
            hits.append(Hit(path, first, kind, snippet(raw, pos, pos + 90), "allow", markers[found][1]))
        elif state == "flag":
            hits.append(Hit(path, first, kind, snippet(raw, pos, pos + 90), "flag", detail))
        elif file_marker:
            used.add(file_marker[0])
            hits.append(Hit(path, first, kind, snippet(raw, pos, pos + 90), "allow", file_marker[1] + " (file)"))
        else:
            hits.append(Hit(path, first, kind, snippet(raw, pos, pos + 90), "FAIL", "no window flag and no allow comment"))
    for n, (k, r) in markers.items():
        if n not in used and len(r) >= 8 and not (k == "allow-file" and n <= 40 and file_marker and any(h.status in ("flag", "allow") for h in hits)):
            errors.append(Hit(path, n, "allow comment", "", "FAIL", "stale marker: no process start here (%s)" % r[:40]))
    return hits + errors


# ---------------------------------------------------------------- repository

def tracked_files(root):
    git = shutil.which("git") or "C:/Program Files/Git/cmd/git.exe"
    names = None
    if (root / ".git").exists():
        try:
            r = subprocess.run([git, "-C", str(root), "ls-files", "-z"], capture_output=True, timeout=60,
                               creationflags=0x08000000 if os.name == "nt" else 0)
            if r.returncode == 0:
                names = [n for n in r.stdout.decode("utf-8", "replace").split("\0") if n]
        except (OSError, subprocess.SubprocessError):
            names = None
    if names is None:
        names = []
        for dirpath, dirnames, filenames in os.walk(root):
            rel = Path(dirpath).relative_to(root).as_posix()
            dirnames[:] = [d for d in dirnames if not (rel == "." and (d.startswith("bazel-") or d in ("build", ".git", "es2-data")))]
            names += [(Path(dirpath) / f).relative_to(root).as_posix() for f in filenames]
    return sorted(n for n in names if os.path.splitext(n)[1].lower() in EXTENSIONS
                  and not n.startswith(SKIP_TOP) and n != SELF)


def scan_repository(root):
    result = []
    for name in tracked_files(root):
        try:
            text = (root / name).read_bytes().decode("utf-8", "replace")
        except OSError:
            continue
        result += scan_text(name, text)
    return result


# ---------------------------------------------------------------- self tests (synthetic snippets)

checks = 0


def check(value, detail=""):
    global checks
    checks += 1
    if not value:
        raise AssertionError(detail)


def states(path, text):
    return [(h.kind, h.status) for h in scan_text(path, text)]


def self_tests():
    # Python
    check(states("a.py", "import subprocess\nsubprocess.run(['x'])\n") == [("subprocess.run", "FAIL")])
    check(states("a.py", "subprocess.run(['x'], creationflags=NO_WINDOW)\n") == [("subprocess.run", "flag")])
    check(states("a.py", "subprocess.run(\n    ['x'],\n    capture_output=True, creationflags=0x08000000)\n") == [("subprocess.run", "flag")])
    check(states("a.py", "subprocess.Popen(cmd, creationflags=0)\n") == [("subprocess.Popen", "FAIL")])
    check(states("a.py", "x = 'subprocess.run(1)'  # subprocess.run(2)\nsubprocess.TimeoutExpired\n") == [])
    check(states("a.py", "os.system('x')\n") == [("os.system", "FAIL")])
    check(states("a.py", "# window-lint: allow inherits console of the Bazel test\nos.system('x')\n") == [("os.system", "allow")])
    check(states("a.py", "os.system('x')  # window-lint: allow inherits console of the Bazel test\n") == [("os.system", "allow")])
    check(states("a.py", "subprocess.run([\n  'x',  # window-lint: allow visible on purpose, interactive\n])\n") == [("subprocess.run", "allow")])
    check(states("a.py", "from subprocess import Popen\nPopen(['x'])\n") == [("Popen", "FAIL")])
    check(states("a.py", "s = \"\"\"function Start-Process {param($f)}\"\"\"\n") == [])
    check(states("a.py", "s = 'Start-Process -FilePath x'\n") == [("Start-Process", "FAIL")])
    check(states("a.py", "s = 'Start-Process -FilePath x -WindowStyle Hidden -PassThru'\n") == [("Start-Process", "flag")])
    check(states("a.py", 'def f():\n    """Start-Process would receive it"""\n    return 1\n') == [])
    # file level marker
    check(states("a.py", "# window-lint: allow-file runs only under Bazel, inherits its console\nsubprocess.run(['x'])\nos.system('y')\n")
          == [("subprocess.run", "allow"), ("os.system", "allow")])
    # reason required, stale markers
    check(states("a.py", "# window-lint: allow\nos.system('x')\n")[0][1] == "FAIL" or "FAIL" in [s for _, s in states("a.py", "# window-lint: allow\nos.system('x')\n")])
    check(("allow comment", "FAIL") in states("a.py", "# window-lint: allow nothing starts here at all\nx = 1\n"))
    # C
    check(states("a.c", "CreateProcessA(0, c, 0, 0, FALSE, 0, 0, 0, &s, &p);\n") == [("CreateProcessA", "FAIL")])
    check(states("a.c", "CreateProcessW(e, c, 0, 0, TRUE, CREATE_NO_WINDOW, 0, 0, &s, &p);\n") == [("CreateProcessW", "flag")])
    check(states("a.c", "DWORD f = CREATE_NO_WINDOW;\nif (!CreateProcessA(0, c, 0, 0, 0, f, 0, 0, &s, &p)) {}\n") == [("CreateProcessA", "flag")])
    check(states("a.c", "x = CreateProcessInfo.hFile; /* CreateProcessA( */ puts(\"_spawnv(\");\n") == [])
    check(states("a.c", "r = _spawnv(_P_WAIT, a, b);\n") == [("_spawnv", "FAIL")])
    check(states("a.c", "/* window-lint: allow inherits console of the Bazel test */\nr = _spawnv(_P_WAIT, a, b);\n") == [("_spawnv", "allow")])
    check(states("a.c", "ShellExecuteA(0, \"open\", f, 0, 0, SW_HIDE);\n") == [("ShellExecuteA", "flag")])
    check(states("a.c", "ShellExecuteA(0, \"open\", f, 0, 0, SW_SHOW);\n") == [("ShellExecuteA", "FAIL")])
    check(states("a.c", "WinExec(c, SW_SHOW);\n") == [("WinExec", "FAIL")])
    # PowerShell
    check(states("a.ps1", "Start-Process -FilePath x -PassThru\n") == [("Start-Process", "FAIL")])
    check(states("a.ps1", "Start-Process -FilePath x `\n  -WindowStyle Hidden -PassThru\n") == [("Start-Process", "flag")])
    check(states("a.ps1", "Start-Process x -NoNewWindow\n") == [("Start-Process", "flag")])
    check(states("a.ps1", "$s = @{FilePath='x'; WindowStyle='Hidden'}\n$p = Start-Process @s\n") == [("Start-Process", "flag")])
    check(states("a.ps1", "$s = @{FilePath='x'}\n$p = Start-Process @s\n") == [("Start-Process", "FAIL")])
    check(states("a.ps1", "# Start-Process x\nWrite-Output 'ok' # Start-Process y\n") == [])
    check(states("a.ps1", "$i = [Diagnostics.ProcessStartInfo]::new()\n$i.CreateNoWindow = $true\n[Diagnostics.Process]::Start($i)\n")
          == [("ProcessStartInfo", "flag")])
    check(states("a.ps1", "$i = [Diagnostics.ProcessStartInfo]::new()\n$i.UseShellExecute = $false\n") == [("ProcessStartInfo", "FAIL")])
    check(states("a.ps1", "# window-lint: allow owner's -Interactive start stays visible\n$p = Start-Process -FilePath $e -WindowStyle $style\n")
          == [("Start-Process", "allow")])
    check(states("a.ps1", "& $tool --x\n") == [])
    check(states("a.psm1", "function Start-Process {param($f)}\n") == [])
    # cmd
    check(states("a.cmd", "@echo off\nstart \"\" notepad\n") == [("start", "FAIL")])
    check(states("a.cmd", "start /B tool\nrem start x\n") == [("start", "flag")])
    check(states("a.cmd", "rem window-lint: allow owner double-clicks this starter, visible on purpose\nstart \"\" game.exe\n") == [("start", "allow")])


def main():
    self_tests()
    hits = scan_repository(ROOT)
    failures = [h for h in hits if h.status == "FAIL"]
    if "--list" in sys.argv[1:] or failures:
        for h in hits:
            print(h.row())
    counts = {s: sum(1 for h in hits if h.status == s) for s in ("flag", "allow", "FAIL")}
    print("process starts: %d (%d with a window flag, %d allowed with a reason, %d without either); %d self checks"
          % (len(hits) - sum(1 for h in hits if h.kind == "allow comment"), counts["flag"], counts["allow"], counts["FAIL"], checks))
    if failures:
        print("FAIL: add a window flag or '<comment> window-lint: allow <reason>' (see tools/test_process_starts.py)")
        return 1
    check(sum(1 for h in hits if h.status in ("flag", "allow")) >= 10, "the scan found almost no process starts: scanner broken")
    return 0


if __name__ == "__main__":
    sys.exit(main())
