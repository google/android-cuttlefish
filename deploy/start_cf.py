#!/usr/bin/python

import os
import subprocess

def doit(args):
  subprocess.check_call([
      'cf',
      '-cpus', args['cpus'],
      '-initrd', '/usr/share/cuttlefish-common/gce_ramdisk.img',
      '-instance', '1',
      '-kernel', os.path.join(args['image_dir'], 'kernel'),
      '-kernel_command_line', os.path.join(args['image_dir'], 'cmdline'),
      '-layout', '/usr/share/cuttlefish-common/vsoc_mem.json',
      '-memory_mb', args['memory_mb'],
      '-system_image_dir', args['image_dir'],
      '-data_image', os.path.join(args['image_dir'], 'userdata.img'),
      '-cache_image', os.path.join(args['image_dir'], 'cache.img'),
      '-logtostderr'])

# TODO(jemoreira): Add command line arguments
doit({'cpus' : '2',
      'image_dir' : '/home/vsoc-01',
      'memory_mb' : '2048'})
