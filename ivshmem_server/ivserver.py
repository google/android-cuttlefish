'''
  ivshmem server main
'''

import argparse
import clientconnection
import channel
import errors
import json
import linuxfd
import os
import select
import subprocess
import sys
import vmconnection
import vsocsharedmem

#
# eventfd for synchronizing ivshmemserver initialization and QEMU launch.
# Also used to pass the vector count to QEMU.
#
efd = {
    'efd' : None
}

class IVServer():
  def __init__(self, args, layout_json):
    self.args = args
    self.layout_json = layout_json

    # Create the SharedMemory. Linux zeroes out the contents initially.
    self.shmobject = vsocsharedmem.VSOCSharedMemory(self.args.size,
                                                    self.args.name)

    # Populate the shared memory with the data from layout description.
    self.shmobject.create_layout(self.layout_json)

    # get the number of vectors. This will be passed to qemu.
    self.num_vectors = self.get_vector_count()

    # Establish the listener socket for QEMU.
    self.vm_listener_socket = channel.start_listener(self.args.path)

    # Establish the listener socket for Clients.
    self.client_listener_socket = channel.start_listener(self.args.client)

    self.vm_connection = None
    self.client_connection = None


  # TODO: Parse from json instead of picking it up from shmobject.
  def get_vector_count(self):
    return self.shmobject.num_vectors

  def serve(self):
    readfdlist = [self.vm_listener_socket, self.client_listener_socket]
    writefdlist = []
    exceptionfdlist = [self.vm_listener_socket, self.client_listener_socket]
    #
    # We are almost ready.
    # There still may be a race between the following select and QEMU
    # execution.
    #
    efd['efd'].write(self.num_vectors)
    while True:
      readable, writeable, exceptions = \
        select.select(readfdlist, writefdlist, exceptionfdlist)

      if exceptions:
        print(exceptions)
        return

      for listenersocket in readable:
        if listenersocket  == self.vm_listener_socket:
          self.handle_new_vm_connection(listenersocket)
        elif listenersocket == self.client_listener_socket:
          self.handle_new_client_connection(listenersocket)


  def handle_new_client_connection(self, listenersocket):
    client_socket = channel.handle_new_connection(listenersocket,
                                                  nonblocking=False)
    print(client_socket)
    self.client_connection = \
      clientconnection.ClientConnection(client_socket,
                                        self.shmobject.posix_shm.fd,
                                        self.layout_json,
                                        0)
    self.client_connection.handshake()


  def handle_new_vm_connection(self, listenersocket):
    vm_socket = channel.handle_new_connection(listenersocket)
    print(vm_socket)
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
      raise argparse.ArgumentTypeError('should be >= 1 but we have %r' % size)
    return size
  parser = argparse.ArgumentParser()
  parser.add_argument('-c', '--cpu', type=unsigned_integer, default=2,
                      help='Number of cpus to use in the guest')
  parser.add_argument('-C', '--client', type=str,
                      default='/tmp/ivshmem_socket_client')
  parser.add_argument('-i', '--image_dir', type=str, required=True,
                      help='Path to the directory of image files for the guest')
  parser.add_argument('-s', '--script_dir', type=str, required=True,
                      help='Path to a directory of scripts')
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


def make_telnet_chardev(args, tcp_base, name):
  return 'socket,nowait,server,host=127.0.0.1,port=%d,ipv4,nodelay,id=%s' % (
      tcp_base + args.instance_number, name)


def make_network_netdev(name, args):
  return ','.join((
      'type=tap',
      'id=%s' % name,
      'ifname=android%d' % args.instance_number,
      'script=%s/android-ifup' % args.script_dir,
      'downscript=%s/android-ifdown' % args.script_dir))


def make_network_device(name, instance_number):
  mac_addr = '00:41:56:44:%02X:%02X' % (
      instance_number / 10, instance_number % 10)
  return 'e1000,netdev=%s,mac=%s' % (name, mac_addr)


def check_version():
  if sys.version_info.major != 3:
    raise errors.VersionException


def main():
  check_version()
  parser = setup_arg_parser()
  args = parser.parse_args()
  layout_json = json.loads(open(args.layoutfile).read())
  efd['efd'] = linuxfd.eventfd(initval=0,
                               semaphore=False,
                               nonBlocking=False,
                               closeOnExec=True)
  pid = os.fork()

  if pid:
    ivshmem_server = IVServer(args, layout_json)
    ivshmem_server.serve()
  else:
    #
    # wait for server to complete initialization
    # the initializing process will also write the
    # number of vectors as a part of signaling us.
    #
    num_vectors = efd['efd'].read()
    qemu_args = []
    qemu_args.append(layout_json['guest']['vmm_path'])
    qemu_args += ('-smp', '%d' % args.cpu)
    qemu_args += ('-m', '%d' % args.memory)
    qemu_args.append('-enable-kvm')
    qemu_args.append('-nographic')
    # Serial port setup
    qemu_args += (
        '-chardev', make_telnet_chardev(args, 10000, 'serial_kernel'),
        '-device', 'isa-serial,chardev=serial_kernel')
    qemu_args += (
        '-device', 'virtio-serial',
        '-chardev', make_telnet_chardev(args, 10100, 'serial_logcat'),
        '-device', 'virtserialport,chardev=serial_logcat')
    # network setup
    qemu_args += (
        '-netdev', make_network_netdev('net0', args),
        '-device', make_network_device('net0', args.instance_number)
    )
    # Configure image files
    launchable = True
    # Use %% to protect the path. The index will be set outside the loop,
    # the path will be set inside.
    DRIVE_VALUE = 'file=%%s,index=%d,format=raw,if=virtio,media=disk'
    for flag, template, path in (
        ('-kernel', '%s', 'kernel'),
        ('-initrd', '%s', 'gce_ramdisk.img'),
        ('-drive', DRIVE_VALUE % 0, 'ramdisk.img'),
        ('-drive', DRIVE_VALUE % 1, 'system.img'),
        ('-drive', DRIVE_VALUE % 2, 'data-%d.img' % args.instance_number),
        ('-drive', DRIVE_VALUE % 3, 'cache-%d.img' % args.instance_number)):
      full_path = os.path.join(args.image_dir, path)
      if not os.path.isfile(full_path):
        print('Missing required image file %s' % full_path)
        launchable = False
      qemu_args += (flag, template % full_path)
    # TODO(romitd): Should path and id be configured per-instance?
    qemu_args += ('-chardev', 'socket,path=%s,id=ivsocket' % (args.path))
    qemu_args += (
        '-device', ('ivshmem-doorbell,chardev=ivsocket,vectors=%d' %
        num_vectors))
    qemu_args += ('-append', ' '.join(layout_json['guest']['kernel_command_line']))
    if not launchable:
      print('Refusing to launch due to errors')
      sys.exit(2)
    subprocess.Popen(qemu_args)


if __name__ == '__main__':
  main()
