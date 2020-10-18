import argparse
import subprocess
import sys

if __name__=='__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('prog')
    parser.add_argument('input')
    parser.add_argument('output')
    parser.add_argument('args', nargs='*')

    args = parser.parse_args()

    unescaped_args = [arg.strip('""') for arg in args.args]

    command = [args.prog] + unescaped_args

    with open(args.output, 'w') as out:
        with open(args.input, 'r') as in_:
            sys.exit(subprocess.run(command, stdin=in_, stdout=out).returncode)
