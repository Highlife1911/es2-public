"""The sim of the native chain in test_game_chain_wasm.py: started by the launcher as the sim
replacement in the game directory, it runs es2sim_window.exe there with the test's --config,
--tick-log and --environment and the launcher's --arguments (SDL's dummy video and audio
drivers), then hands the shell its next headless script (--next copied to --script) and keeps
the finished shell's report as <report>.1, as the web chain's per-start arguments do. Its exit
status is es2sim_window's.

Standard library only.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

NO_WINDOW = getattr(subprocess, 'CREATE_NO_WINDOW', 0)


def main(argv):
    parser = argparse.ArgumentParser()
    for name in ('--exe', '--config', '--tick-log', '--environment', '--script', '--next', '--report', '--arguments'):
        parser.add_argument(name, required=True)
    args = parser.parse_args(argv)
    env = dict(os.environ)
    for key in ('SDL_VIDEO_DRIVER', 'SDL_AUDIO_DRIVER'):
        env[key] = 'dummy'
    run = subprocess.run([args.exe, '--config', args.config, '--tick-log', args.tick_log, '--environment',
                          args.environment, '--arguments', args.arguments], env=env, timeout=600,
                         creationflags=NO_WINDOW)
    report = Path(args.report)
    if report.exists():
        shutil.copyfile(report, report.with_name(report.name + '.1'))
    shutil.copyfile(args.next, args.script)
    return run.returncode


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
