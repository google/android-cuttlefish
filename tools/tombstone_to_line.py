#!/usr/bin/python
#
# Copyright (C) 2018 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0(the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
"""Use addr2line to interpret tombstone contents

The defaults should work if this is run after running lunch.
"""

from __future__ import print_function
import argparse
import collections
import functools
import multiprocessing
import os
import re
import subprocess
import sys


# Patterns of things that we might want to match.

patterns = [
    (re.compile('(.* pc )([0-9a-f]+) +([^ ]+) .*'), 1, 3, 2),
    (re.compile('(.*)#[0-9]+ 0x[0-9a-f]+ +\((.*)\+0x([0-9a-f]+)\)'), 1, 2, 3)]


LookupInfo = collections.namedtuple('LookupInfo',
                                    ['line_number', 'details', 'file_name'])


def lookup_addr(args, object_path, address):
  try:
    if object_path[0] == os.path.sep:
      object_path = object_path[1:]
    parms = [args.addr2line, '-e',
             os.path.join(args.symbols, object_path), address]
    details = subprocess.check_output(parms).strip().split(':')
    return LookupInfo(
        line_number=details[-1],
        details=details,
        file_name=':'.join(details[:-1]))
  except subprocess.CalledProcessError:
    return None


def simple_match(line, info, indent, out_file):
  print('{} // From {}:{}'.format(
      line, info.file_name, info.line_number), file=out_file)


def source_match(line, info, indent, out_file):
  source = ''
  try:
    with open(info.file_name, 'r') as f:
      for i in range(int(info.line_number.split(' ')[0])):
        source = f.readline()
  # Fall back to the simple formatter on any error
  except Exception:
    simple_match(line, info, indent, out_file)
    return
  print(line, file=out_file)
  print('{}// From {}:{}'.format(
      ' ' * indent, info.file_name, info.line_number), file=out_file)
  print('{}  {}'.format(' ' * indent, ' '.join(source.strip().split())),
        file=out_file)


def process(in_file, out_file, args):
  for line in in_file:
    line = line.rstrip()
    groups = None
    for p in patterns:
      groups = p[0].match(line)
      if groups:
        break
    info = None
    if groups is not None:
      info = lookup_addr(args, groups.group(p[2]), groups.group(p[3]))
    if info is None:
      print(line, file=out_file)
      continue
    if args.source:
      source_match(line, info, len(groups.group(p[1])), out_file)
    else:
      simple_match(line, info, len(groups.group(p[1])), out_file)


def process_file(path, args):
    with open(path + args.suffix, 'w') as out_file:
      with open(path, 'r') as in_file:
        process(in_file, out_file, args)


def common_arg_parser():
  parser = argparse.ArgumentParser(description=
                                   'Add line information to a tombstone')
  parser.add_argument('--addr2line', type=str,
                      help='Path to addr2line',
                      default=os.path.join(
                          os.environ.get('ANDROID_TOOLCHAIN', ''),
                          'x86_64-linux-android-addr2line'))
  parser.add_argument('files', metavar='FILE', type=str, nargs='+',
                      help='a list of files to process')
  parser.add_argument('--jobs', type=int, default=32,
                      help='Number of parallel jobs to run')
  parser.add_argument('--source', default=False, action='store_true',
                      help='Attempt to print the source')
  parser.add_argument('--suffix', type=str, default='.txt',
                      help='Suffix to add to the processed file')
  return parser



def process_all(args):
  multiprocessing.Pool(32).map(functools.partial(process_file, args=args),
                               args.files)


if __name__ == '__main__':
  parser = common_arg_parser()
  parser.add_argument('--symbols', type=str,
                      help='Path to the symbols',
                      default=os.path.join(
                          os.environ.get('ANDROID_PRODUCT_OUT', ''), 'symbols'))
  process_all(parser.parse_args())
