'''
    ivshmem server main
'''
# pylint: disable=too-many-instance-attributes,relative-beyond-top-level

import argparse
import json
import logging
import os
import signal
import sys
from .libvirt_client import LibVirtClient
from .guest_definition import GuestDefinition

LOG_FORMAT="%(levelname)1.1s %(asctime)s %(process)d %(filename)s:%(lineno)d] %(message)s"

def setup_arg_parser():
    def unsigned_integer(size):
        size = int(size)
        if size < 1:
            raise argparse.ArgumentTypeError(
                'should be >= 1 but we have %r' % size)
        return size
    parser = argparse.ArgumentParser()
    parser.add_argument('-c', '--cpu', type=unsigned_integer, default=2,
                        help='Number of cpus to use in the guest')
    parser.add_argument('-C', '--client', type=str,
                        help = 'Location of the HALD client socket, ' +
                        'default=/tmp/ivshmem_socket_client',
                        default='/tmp/ivshmem_socket_client')
    parser.add_argument('-i', '--image_dir', type=str, required=True,
                        help='Path to the directory of image files for the guest')
    parser.add_argument('-I', '--instance_number', type=unsigned_integer,
                        default=1,
                        help='Instance number for this device')
    parser.add_argument('-L', '--layoutfile', type=str, required=True)
    parser.add_argument('-M', '--memory', type=unsigned_integer, default=2048,
                        help='Size of the non-shared guest RAM in MiB')
    parser.add_argument('-P', '--path', type=str, default='/tmp/ivshmem_socket_qemu',
                        help='Location of the QEmu socket, default=/tmp/ivshmem_socket_qemu')
    return parser


def main():
    logging.basicConfig(format=LOG_FORMAT)
    log = logging.getLogger()

    try:
        parser = setup_arg_parser()
        args = parser.parse_args()

        lvc = LibVirtClient()

        layout_json = json.loads(open(args.layoutfile).read())

        if 'movbe' not in lvc.get_cpu_features():
            log.warning('host CPU may not support movbe instruction')

        guest = GuestDefinition(lvc)

        guest.set_num_vcpus(args.cpu)
        guest.set_memory_mb(args.memory)
        guest.set_instance_id(args.instance_number)
        guest.set_cmdline(' '.join(layout_json['guest']['kernel_command_line']))
        guest.set_kernel(os.path.join(args.image_dir, 'kernel'))
        guest.set_initrd(os.path.join(args.image_dir, 'gce_ramdisk.img'))

        guest.set_cf_ramdisk_path(os.path.join(args.image_dir, 'ramdisk.img'))
        guest.set_cf_system_path(os.path.join(args.image_dir, 'system.img'))
        guest.set_cf_data_path(os.path.join(args.image_dir, 'data.img'))
        guest.set_cf_cache_path(os.path.join(args.image_dir, 'cache.img'))
        guest.set_net_mobile_bridge('abr0')

        guest.set_ivshmem_vectors(len(layout_json['vsoc_device_regions']))
        guest.set_ivshmem_socket_path(args.path)

        guest.set_vmm_path(layout_json['guest']['vmm_path'])

        log.info('Creating virtual instance...')
        dom = lvc.create_instance(guest.to_xml())
        log.info('VM ready.')
        dom.resume()
        try:
            signal.pause()
        except KeyboardInterrupt:
            log.info('Stopping IVShMem server')
            dom.destroy()


    except Exception as exception:
        log.exception('Could not start VM: %s', exception)


if __name__ == '__main__':
    main()
