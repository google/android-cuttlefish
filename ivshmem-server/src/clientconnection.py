'''
  ClientConnection related.
'''

import channel

class ClientConnection():
  def __init__(self, client_socket, shmfd, layout_json, protocol_version=0):
    self.shmfd = shmfd
    self.client_socket = client_socket
    self.layout_json = layout_json
    self.proto_ver = protocol_version

  def handshake(self):
    #
    #   ivshmem_client <--> ivshmem_server handshake.
    #
    #   Client -> 'GET PROTOCOL_VER'
    #   Server -> 'PROTOCOL_VER 0'
    #   Client -> INFORM REGION_NAME_LEN: 0x0000000a
    #   Client -> GET REGION: HW_COMPOSER
    #   Server -> 0xffffffff(If region name not found)
    #   Server -> 0xAABBC000 (region start offset)
    #   Server -> 0xAABBC0000 (region end offset)
    #   Server -> Number of nodes. (In 0xAABBCCDD format)
    #   For each node
    #      Server -> Offset of lockaddress
    #   Server -> <Send cmsg with shmfd>
    #

    msg = channel.recv_msg(self.client_socket, len('GET PROTOCOL_VER'))
    print('server got %s' % msg)
    if msg == 'GET PROTOCOL_VER':
      channel.send_msg_utf8(self.client_socket, '0x00000000')

    msg = channel.recv_msg(self.client_socket,
                           len('INFORM REGION_NAME_LEN: 0xAABBCCDD'))
    print('server got %s' % msg)

    if msg[:len('INFORM REGION_NAME_LEN: 0x')] == 'INFORM REGION_NAME_LEN: 0x':
      region_name_len = int(msg[len('INFORM REGION_NAME_LEN: '):], base=16)
      print(region_name_len)

    msg = channel.recv_msg(self.client_socket, region_name_len)
    print('server got region_name: %s' % msg)

    device_region = None
    for vsoc_device_region in self.layout_json['vsoc_device_regions']:
      # TODO:
      # read from the shared memory.
      if vsoc_device_region['comment'].upper() == msg:
        print('found region for %s' % msg)
        found_region = True
        device_region = vsoc_device_region

    if device_region:
      # Send region start offset
      channel.send_msg_utf8(self.client_socket,
                           '0x%08x' % \
                            int(device_region['region_begin_offset']))
      # Send region end offset
      channel.send_msg_utf8(self.client_socket,
                           '0x%08x' % \
                            int(device_region['region_end_offset']))
    else:
      # send MAX_UINT32 if region not found.
      channel.send_msg_utf8(self.client_socket, '0xffffffff')
      self.client_socket.close()
      print('Closing connection')
      # TODO:
      # Raise a suitable Exception.
      return

    node_count = int(device_region['host_to_guest_signal_table']['num_nodes'])
    print('server sending node count %s ' % ('0x%08x' % node_count))
    channel.send_msg_utf8(self.client_socket, '0x%08x' % node_count)

    print('server sending shm fd %d' % self.shmfd)
    channel.send_ctrl_msg(self.client_socket, self.shmfd, 0)
    print('server closing connection')
    self.client_socket.close()

