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
  parser.add_argument('-N', '--name', type=str, default='ivshmem',
                      help='Name of the POSIX shared memory segment')
  parser.add_argument('-S', '--size', type=unsigned_integer, default=4,
                      help='Size of shared memory region in MiB, default=4MiB')
  parser.add_argument('-P', '--path', type=str, default='/tmp/ivshmem_socket',
                      help='Path to UNIX Domain Socket, default=/tmp/ivshmem_socket')
  parser.add_argument('-C', '--client', type=str, default='/tmp/ivshmem_socket_client')
  parser.add_argument('-L', '--layoutfile', type=str, required=True)
  return parser


def main():
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
    qemu_args.append(layout_json['qemu']['path'])
    qemu_args.append(layout_json['qemu']['memory'])
    qemu_args.append(layout_json['qemu']['kvm'])
    qemu_args.append(layout_json['qemu']['drive'])
    qemu_args.append(layout_json['qemu']['chardev'])
    qemu_args.append(layout_json['qemu']['device']+',vectors=' + \
                     str(num_vectors))
    qemu_args.append(layout_json['qemu']['net'])
    subprocess.Popen(' '.join(qemu_args), shell=True)

def check_version():
  if sys.version_info.major != 3:
    raise errors.VersionException

if __name__ == '__main__':
  check_version()
  main()

