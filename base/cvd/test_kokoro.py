#!/usr/bin/python3

"""Testing out kokoro permissions..."""

import argparse
import os
import subprocess


def _do(cmd):
    print('Running: ' + ' '.join(cmd))
    subprocess.run(cmd, check=True)


def _get(args):
    print('Running: ' + ' '.join(args))
    return subprocess.run(args, check=True, text=True, stdout=subprocess.PIPE).stdout.strip()

def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()

    parser.add_argument('--outdir', type=str, required=True)

    return parser.parse_args()


def main():
    args = _parse_args()

    out_dir = args.outdir
    out_c_file = out_dir + '/test_kokoro.cpp'
    out_h_file = out_dir + '/test_kokoro.h'

    print('jasonjason starting')

    print('Current working directory: ', _get(['pwd']))

    print('Out directory permissions: ', _get(['ls', '-alh', out_dir]))

    print('Current user: ', _get(['whoami']))

    print('Current user groups: ', _get(['groups']))

    print('Out directory realpath: ', _get(['realpath', out_dir]))

    with open(out_c_file, 'w', encoding='utf-8') as f:
        f.write("""
            #include <iostream>
            int main() {
            std::cout << "Hello, world!" << std::endl;
            return 0;
            }
        """)
    with open(out_h_file, 'w', encoding='utf-8') as f:
        f.write("""
            // does nothing
        """)

    print('jasonjason success!')


if __name__ == '__main__':
    main()