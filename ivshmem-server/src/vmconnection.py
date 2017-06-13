'''
  VMConnection related.
'''

import channel
import linuxfd

class VMConnection():
  def __init__(self, layout_json, posix_shm, vm_socket, vector_count, hostid,
               vmid, protocol_version=0):
    self.hostid = hostid
    self.vmid = vmid
    self.shm = posix_shm
    self.vm_socket = vm_socket
    self.nvecs = vector_count
    self.proto_ver = protocol_version
    self.layout_json = layout_json
    self.eventfds = {}

  def handshake(self):
    channel.send_msg_8(self.vm_socket, self.proto_ver)
    channel.send_msg_8(self.vm_socket, self.vmid)
    channel.send_ctrl_msg(self.vm_socket, self.shm.fd, -1)
    # self.create_eventfds()

    # send the eventfds
    for region in self.layout_json['vsoc_device_regions']:
      for eventfd in region['host_to_guest_signal_table']['eventfds']:
        channel.send_ctrl_msg(self.vm_socket, eventfd.fileno(), self.vmid)

    for region in self.layout_json['vsoc_device_regions']:
      for eventfd in region['guest_to_host_signal_table']['eventfds']:
        channel.send_ctrl_msg(self.vm_socket, eventfd.fileno(), self.hostid)
    '''
    for eventfd in self.eventfds[self.vmid]:
      channel.send_ctrl_msg(self.vm_socket, eventfd.fileno(), self.vmid)
    for eventfd in self.eventfds[self.hostid]:
      channel.send_ctrl_msg(self.vm_socket, eventfd.fileno(), self.hostid)
    '''

  def create_eventfds(self):
    # Create as many eventfds as there are vectors
    guest_eventfds=[]
    host_eventfds=[]
    for count in range(self.nvecs):
      host_eventfds.append(linuxfd.eventfd(initval=0, semaphore=False,
                                           nonBlocking=True, closeOnExec=True))
      guest_eventfds.append(linuxfd.eventfd(initval=0, semaphore=False,
                                            nonBlocking=False, closeOnExec=True))

    self.eventfds[self.hostid] = host_eventfds
    self.eventfds[self.vmid] = guest_eventfds

