'''
 VSOC Shared Memory backed by POSIX shared memory.
 Also creates the initial vSOC layout.
'''

import linuxfd
import mmap
import os
import posix_ipc
import struct

class VSOCSharedMemory():
  def __init__(self, size, name='ivshmem'):
    self.shm_size = size << 20
    self.posix_shm = None
    self.create_posix_shm(size, name)
    self.num_vectors = 0

  def create_posix_shm(self, size, name='ivshmem'):
    self.posix_shm = posix_ipc.SharedMemory(name,
                                            flags=os.O_CREAT,
                                            size=self.shm_size)

  def create_layout(self, layout, major_version=0, minor_version=1):
    offset = 0
    shmmap = mmap.mmap(self.posix_shm.fd, 0)

    #
    # shared memory layout based on
    # project: kernel/private/gce_x86
    # file: drivers/staging/android/uapi/vsoc_shm.h
    # commit-id: 2bc0d6b47c4a8d8204faecb7016af09a28e8c3ad
    # TODO:
    # We need a way to synchronize the changes in the
    # sources with the packing below.
    #

    header_struct = struct.Struct('HHIII')
    header_struct.pack_into(shmmap, offset,
                            major_version,
                            minor_version,
                            shmmap.size(),
                            int(layout['vsoc_shm_layout_descriptor']
                                      ['region_count']),
                            int(layout['vsoc_shm_layout_descriptor']
                                      ['vsoc_region_desc_offset'])
                            )
    region_descriptor_offset = int(layout['vsoc_shm_layout_descriptor']
                                         ['vsoc_region_desc_offset'])
    offset += region_descriptor_offset

    vsoc_device_struct = struct.Struct('HHIIIIIII16s')

    for region in layout['vsoc_device_regions']:
      self.num_vectors += 1

      region['eventfds'] = {}
      region['eventfds']['guest_to_host'] = \
        linuxfd.eventfd(initval=0,
                        semaphore=False,
                        nonBlocking=True,
                        closeOnExec=True)

      region['eventfds']['host_to_guest'] = \
        linuxfd.eventfd(initval=0,
                        semaphore=False,
                        nonBlocking=False,
                        closeOnExec=True)

      vsoc_device_struct. \
        pack_into(shmmap, offset,
                  int(region['current_version']),
                  int(region['min_compatible_version']),
                  int(region['region_begin_offset']),
                  int(region['region_end_offset']),
                  int(region['offset_of_region_data']),
                  int(region['guest_to_host_signal_table']['num_nodes']),
                  int(region['guest_to_host_signal_table']['offset']),
                  int(region['host_to_guest_signal_table']['num_nodes']),
                  int(region['host_to_guest_signal_table']['offset']),
                  bytes(region['device_name'], encoding='ascii')
                )
      offset += vsoc_device_struct.size

    # We need atleast one vector to start QEMU.
    # TODO: Perhaps throw an exception here and bail out early.
    if self.num_vectors == 0:
      self.num_vectors = 1


