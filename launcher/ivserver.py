'''
    ivshmem server main
'''
# pylint: disable=too-many-instance-attributes,relative-beyond-top-level

import argparse
import json
import os
import select
import signal
import sys
import threading
import glog
from .libvirt_client import LibVirtClient
from .guest_definition import GuestDefinition
from . import clientconnection
from . import channel
from . import vmconnection
from . import vsocsharedmem

class IVServer(object):
    def __init__(self, region_name, region_size,
                 vm_socket_path, client_socket_path, layout_json):
        self.layout_json = layout_json

        # Create the SharedMemory. Linux zeroes out the contents initially.
        self.shmobject = vsocsharedmem.VSOCSharedMemory(region_size, region_name)

        # Populate the shared memory with the data from layout description.
        self.shmobject.create_layout(self.layout_json)

        # get the number of vectors. This will be passed to qemu.
        self.num_vectors = self.get_vector_count()

        # Establish the listener socket for QEMU.
        self.vm_listener_socket_path = vm_socket_path
        self.vm_listener_socket = channel.start_listener(vm_socket_path)

        # Establish the listener socket for Clients.
        self.client_listener_socket = channel.start_listener(client_socket_path)

        self.vm_connection = None
        self.client_connection = None

        # _control_channel and _thread_channel are two ends of the same, control pipe.
        # _thread_channel will be used by the serving thread until pipe is closed.
        (self._control_channel, self._thread_channel) = os.pipe()
        self._thread = threading.Thread(target=self._serve_in_background)

    def get_vector_count(self):
        # TODO: Parse from json instead of picking it up from shmobject.
        return self.shmobject.num_vectors

    def get_socket_path(self):
        return self.vm_listener_socket_path

    def serve(self):
        """Begin serving IVShMem data to QEmu.
        """
        self._thread.start()

    def stop(self):
        """Stop serving data to QEmu and join the serving thread.
        """
        os.close(self._control_channel)
        self._thread.join()

    def _serve_in_background(self):
        readfdlist = [
            self.vm_listener_socket,
            self.client_listener_socket,
            self._thread_channel
        ]

        while True:
            readable, _, _ = select.select(readfdlist, [], [])

            for client in readable:
                if client == self.vm_listener_socket:
                    self.handle_new_vm_connection(client)
                elif client == self.client_listener_socket:
                    self.handle_new_client_connection(client)
                elif client == self._thread_channel:
                    # For now we do not expect any communication over pipe.
                    # Since this bit of code is going away, we'll just assume
                    # that the parent wants the thread to exit.
                    return


    def handle_new_client_connection(self, listenersocket):
        client_socket = channel.handle_new_connection(listenersocket,
                                                      nonblocking=False)
        self.client_connection = \
            clientconnection.ClientConnection(client_socket,
                                              self.shmobject.posix_shm.fd,
                                              self.layout_json,
                                              0)
        self.client_connection.handshake()

    def handle_new_vm_connection(self, listenersocket):
        vm_socket = channel.handle_new_connection(listenersocket)
        self.vm_connection = vmconnection.VMConnection(self.layout_json,
                                                       self.shmobject.posix_shm,
                                                       vm_socket,
                                                       self.num_vectors,
                                                       hostid=0,
                                                       vmid=1)
        self.vm_connection.handshake()


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
                        default='/tmp/ivshmem_socket_client')
    parser.add_argument('-i', '--image_dir', type=str, required=True,
                        help='Path to the directory of image files for the guest')
    parser.add_argument('-I', '--instance_number', type=unsigned_integer,
                        default=1,
                        help='Instance number for this device')
    parser.add_argument('-L', '--layoutfile', type=str, required=True)
    parser.add_argument('-M', '--memory', type=unsigned_integer, default=2048,
                        help='Size of the non-shared guest RAM in MiB')
    parser.add_argument('-N', '--name', type=str, default='ivshmem',
                        help='Name of the POSIX shared memory segment')
    parser.add_argument('-P', '--path', type=str, default='/tmp/ivshmem_socket',
                        help='Path to UNIX Domain Socket, default=/tmp/ivshmem_socket')
    parser.add_argument('-S', '--size', type=unsigned_integer, default=4,
                        help='Size of shared memory region in MiB, default=4MiB')
    return parser


def check_version():
    if sys.version_info.major != 3:
        glog.fatal('This program requires python3 to work.')
        sys.exit(1)


def main():
    glog.setLevel(glog.INFO)
    try:
        check_version()
        parser = setup_arg_parser()
        args = parser.parse_args()

        lvc = LibVirtClient()

        layout_json = json.loads(open(args.layoutfile).read())
        ivshmem_server = IVServer(args.name, args.size, args.path, args.client, layout_json)

        if 'movbe' not in lvc.get_cpu_features():
            glog.warning('host CPU may not support movbe instruction')

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

        guest.set_ivshmem_vectors(ivshmem_server.get_vector_count())
        guest.set_ivshmem_socket_path(ivshmem_server.get_socket_path())

        guest.set_vmm_path(layout_json['guest']['vmm_path'])

        # Accept and process IVShMem connections from QEmu.
        ivshmem_server.serve()

        glog.info('Creating virtual instance...')
        dom = lvc.create_instance(guest.to_xml())
        glog.info('VM ready.')
        dom.resume()
        try:
            signal.pause()
        except KeyboardInterrupt:
            glog.info('Stopping IVShMem server')
            dom.destroy()
            ivshmem_server.stop()


    except Exception as exception:
        glog.exception('Could not start VM: %s', exception)


if __name__ == '__main__':
    main()
