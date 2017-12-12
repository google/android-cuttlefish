#!/usr/bin/env python
#
#Copyright(C) 2017 The Android Open Source Project
#
#Licensed under the Apache License, Version 2.0(the "License");
#you may not use this file except in compliance with the License.
#You may obtain a copy of the License at
#
#http:  // www.apache.org/licenses/LICENSE-2.0
#
#Unless required by applicable law or agreed to in writing, software
#distributed under the License is distributed on an "AS IS" BASIS,
#WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#See the License for the specific language governing permissions and
#limitations under the License.
#
"""Unpacks boot.img files.

Most Android devices have boot roms that interpret the boot.img file. The
VMs that Cuttlefish uses don't handle Android's format. This breaks the
kernel image, ram disk, and kernel command line into indivdual files.
"""

import argparse
import os
import re
import struct
import subprocess


def pad(size, value):
    """Adds up to size bytes to value to make value mod size == 0"""
    return (value + size - 1)/size*size

def extract_file(f, seekpt, size, dest_path):
    """Creates a new file and then copies bytes to fill it.

    Args:
      f: The source file
      seekpt: The offset in the source file to copy from
      size: the number of bytes to copy
      dest_path: the pathname of the file that should be created
    """
    f.seek(seekpt)
    with open(dest_path, 'wb') as out:
        while size:
          bytes=min(size, 8192)
          out.write(f.read(bytes))
          size -= bytes


def unpack_boot_img(path, out_dir):
  """Unpacks the boot image, writing the parts to files in out_dir.

  Args:
    path: The path to the boot.img
    out_dir: The path to an existing direction that should hold the parts.
  """

#This header is defined in host / linux - x86 / bin / mkbootimg
  header = (
      ('signature', '8s', 8),
      ('kernel_size', 'I', 4),
      ('kernel_addr', 'I', 4),
      ('ramdisk_size', 'I', 4),
      ('ramdisk_addr', 'I', 4),
      ('second_loader_size', 'I', 4),
      ('second_loader_addr', 'I', 4),
      ('tags_addr', 'I', 4),
      ('page_size', 'I', 4),
      ('reserved', 'I', 4),
      ('version', 'I', 4),
      ('device', '16s', 16),
      ('cmdline_1', '512s', 512),
      ('sha', '32s', 32),
      ('cmdline_2', '1024s', 1024)
  )

  pack_string = ''
  pack_size = 0
  for field in header:
    pack_string += field[1]
    pack_size += field[2]
  with open(path, 'rb') as infile:
    values = struct.unpack(pack_string, infile.read(pack_size))
    fields = dict(zip([f[0] for f in header], values))
    page_size = int(fields['page_size'])
    cmdline = (fields['cmdline_1'] + fields['cmdline_2']).split('\0')[0]
    with open(os.path.join(out_dir, 'cmdline'), 'w') as out:
      out.write(cmdline)
    offset = pad(page_size, pack_size)
    kernel_size = int(fields['kernel_size'])
    extract_file(infile, offset, kernel_size, os.path.join(out_dir, 'kernel'))
    offset += pad(page_size, kernel_size)
    ramdisk_size = int(fields['ramdisk_size'])
    extract_file(infile, offset, ramdisk_size, os.path.join(out_dir, 'ramdisk.img'))


def main():
    parser = argparse.ArgumentParser(
        description='Unpacks a boot.img')
    parser.add_argument('-dest', type=str, default='.',
        help='Destination directory')
    parser.add_argument('-boot_img',type=str,
            help='Path to the boot.img')
    args = parser.parse_args()
    unpack_boot_img(args.boot_img, args.dest)


if __name__ == '__main__':
    main()
