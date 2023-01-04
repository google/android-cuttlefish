#!/usr/bin/python
#
# Copyright 2018 The Android Open-Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
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

"""Upload a local build to Google Compute Engine and run it."""

import argparse
import glob
import os
import subprocess


def upload_artifacts(args):
  dir = os.getcwd()
  try:
    os.chdir(args.image_dir)
    images = glob.glob('*.img') + ["bootloader"]
    if len(images) == 0:
      raise OSError('File not found: ' + args.image_dir + '/*.img')
    subprocess.check_call(
      'tar -c -f - --lzop -S ' + ' '.join(images) +
        ' | ssh %s@%s -- tar -x -f - --lzop -S' % (
          args.user,
          args.ip),
      shell=True)
  finally:
    os.chdir(dir)

  host_package = os.path.join(args.host_dir, 'cvd-host_package.tar.gz')
  # host_package
  subprocess.check_call(
      'ssh %s@%s -- tar -x -z -f - < %s' % (
          args.user,
          args.ip,
          host_package),
      shell=True)


def launch_cvd(args):
  subprocess.check_call(
      'ssh %s@%s -- bin/launch_cvd %s' % (
          args.user,
          args.ip,
          ' '.join(args.runner_args)
      ),
      shell=True)


def stop_cvd(args):
  subprocess.call(
      'ssh %s@%s -- bin/stop_cvd' % (
          args.user,
          args.ip),
      shell=True)


def main():
  parser = argparse.ArgumentParser(
      description='Upload a local build to Google Compute Engine and run it')
  parser.add_argument(
      '-host_dir',
      type=str,
      default=os.environ.get('ANDROID_HOST_OUT', '.'),
      help='path to soong host out directory')
  parser.add_argument(
      '-image_dir',
      type=str,
      default=os.environ.get('ANDROID_PRODUCT_OUT', '.'),
      help='path to the img files')
  parser.add_argument(
      '-user', type=str, default='vsoc-01',
      help='user to update on the instance')
  parser.add_argument(
      '-ip', type=str,
      help='ip address of the board')
  parser.add_argument(
      '-launch', default=False,
      action='store_true',
      help='launch the device')
  parser.add_argument('runner_args', nargs='*', help='launch_cvd arguments')
  args = parser.parse_args()
  stop_cvd(args)
  upload_artifacts(args)
  if args.launch:
    launch_cvd(args)


if __name__ == '__main__':
  main()
