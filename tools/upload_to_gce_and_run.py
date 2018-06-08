#!/usr/bin/python

"""Upload a local build to Google Compute Engine and run it."""

import argparse
import glob
import os
import subprocess


def upload_artifacts(args):
  image_pat = os.path.join(args.dist_dir, '%s-img-*.zip' % args.product)
  images = glob.glob(image_pat)
  if len(images) == 0:
    raise OSError('File not found: %s' + image_pat)
  if len(images) > 1:
    raise OSError('%s matches multiple images: %s' % (
        image_pat, images))
  subprocess.check_call(
      'gcloud compute ssh %s@%s -- /usr/bin/install_zip.sh . < %s' % (
          args.user,
          args.instance,
          images[0]),
      shell=True)

  host_package = os.path.join(args.dist_dir, 'cvd-host_package.tar.gz')
  # host_package
  subprocess.check_call(
      'gcloud compute ssh %s@%s -- tar -x -z -f - < %s' % (
          args.user,
          args.instance,
          host_package),
      shell=True)


def launch_cvd(args):
  subprocess.check_call(
      'gcloud compute ssh %s@%s -- bin/launch_cvd' % (
          args.user,
          args.instance),
      shell=True)


def stop_cvd(args):
  subprocess.call(
      'gcloud compute ssh %s@%s -- bin/stop_cvd' % (
          args.user,
          args.instance),
      shell=True)


def main():
  parser = argparse.ArgumentParser(
      description='Fetch images from a compute engine bucket')
  parser.add_argument(
      '-dist_dir',
      type=str,
      default=os.path.join(os.environ.get('ANDROID_BUILD_TOP', '.'),
                           'out', 'dist'),
      help='path to the dist directory')
  parser.add_argument(
      '-instance', type=str, required=True,
      help='instance to update')
  parser.add_argument(
      '-product',
      type=str,
      default=os.environ.get('TARGET_PRODUCT', 'cf_x86_phone'),
      help='product to upload')
  parser.add_argument(
      '-user', type=str, default='vsoc-01',
      help='user to update on the instance')
  args = parser.parse_args()
  stop_cvd(args)
  upload_artifacts(args)
  launch_cvd(args)


if __name__ == '__main__':
  main()
