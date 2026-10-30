"""Stub for es2sim in the launcher chain test (test_launch_chain_wasm.py): no
simulation runs. Started by the launcher as the sim replacement in a temporary
game directory, it does what the shell sees of a DBSIM mission and returns 3:

* DATA/results.dat and DATA/mission.var from a recorded DBSIM output (--golden,
  an oracle suite's output directory, read only);
* the headless script of the next shell start (--next copied to --script) and
  the finished shell's report kept as <report>.1;
* one line "<--arguments>" appended to --log.

Standard library only.
"""
import argparse
from pathlib import Path
import shutil
import sys


def main(argv):
    parser = argparse.ArgumentParser()
    for name in ('--golden', '--script', '--next', '--report', '--log', '--arguments'):
        parser.add_argument(name, required=True)
    args = parser.parse_args(argv)
    golden = Path(args.golden)
    for name in ('results.dat', 'mission.var'):
        shutil.copyfile(golden / name, Path('DATA') / name)
    report = Path(args.report)
    if report.exists():
        shutil.copyfile(report, report.with_name(report.name + '.1'))
    shutil.copyfile(args.next, args.script)
    with open(args.log, 'a', encoding='latin-1', newline='\n') as log:
        log.write(args.arguments + '\n')
    return 3


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
