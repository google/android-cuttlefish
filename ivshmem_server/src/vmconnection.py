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

  def handshake(self):
    channel.send_msg_8(self.vm_socket, self.proto_ver)
    channel.send_msg_8(self.vm_socket, self.vmid)
    channel.send_ctrl_msg(self.vm_socket, self.shm.fd, -1)

    # send the eventfds
    for region in self.layout_json['vsoc_device_regions']:
      eventfd = region['eventfds']['host_to_guest']
      channel.send_ctrl_msg(self.vm_socket, eventfd.fileno(), self.vmid)

    for region in self.layout_json['vsoc_device_regions']:
      eventfd = region['eventfds']['guest_to_host']
      channel.send_ctrl_msg(self.vm_socket, eventfd.fileno(), self.hostid)


