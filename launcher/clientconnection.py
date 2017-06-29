'''
    ClientConnection related.
'''

import glog

from . import channel


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
        #   Server -> '0x000000000'
        #   Client -> INFORM REGION_NAME_LEN: 0x0000000a
        #   Client -> GET REGION: HW_COMPOSER
        #   Server -> 0xffffffff(If region name not found)
        #   Server -> 0xAABBC000 (region start offset)
        #   Server -> 0xAABBC0000 (region end offset)
        #   Server -> <Send cmsg with guest_to_host eventfd>
        #   Server -> <Send cmsg with host_to_guest eventfd>
        #   Server -> <Send cmsg with shmfd>
        #

        msg = channel.recv_msg(self.client_socket, len('GET PROTOCOL_VER'))
        glog.debug('server got %s' % msg)
        if msg == 'GET PROTOCOL_VER':
            channel.send_msg_utf8(self.client_socket, '0x00000000')

        msg = channel.recv_msg(self.client_socket,
                               len('INFORM REGION_NAME_LEN: 0xAABBCCDD'))
        glog.debug('server got %s' % msg)

        if msg[:len('INFORM REGION_NAME_LEN: 0x')] == 'INFORM REGION_NAME_LEN: 0x':
            region_name_len = int(
                msg[len('INFORM REGION_NAME_LEN: '):], base=16)
            glog.debug(region_name_len)

        msg = channel.recv_msg(self.client_socket, region_name_len)
        glog.debug('server got region_name: %s' % msg)

        device_region = None
        for vsoc_device_region in self.layout_json['vsoc_device_regions']:
            # TODO:
            # read from the shared memory.
            if vsoc_device_region['device_name'] == msg:
                glog.debug('found region for %s' % msg)
                device_region = vsoc_device_region

        if device_region:
            # Send region start offset
            channel.send_msg_utf8(self.client_socket,
                                  '0x%08x' %
                                  int(device_region['region_begin_offset']))
            # Send region end offset
            channel.send_msg_utf8(self.client_socket,
                                  '0x%08x' %
                                  int(device_region['region_end_offset']))
        else:
            # send MAX_UINT32 if region not found.
            channel.send_msg_utf8(self.client_socket, '0xffffffff')
            self.client_socket.close()
            glog.debug('Closing connection')
            # TODO:
            # Raise a suitable Exception.
            return

        glog.debug('sending guest to host eventfd to client:')
        glog.debug(device_region['eventfds']['guest_to_host'])
        eventfd = device_region['eventfds']['guest_to_host']
        channel.send_ctrl_msg(self.client_socket, eventfd.fileno(), 0)

        glog.debug('sending host to guest eventfd to client:')
        eventfd = device_region['eventfds']['host_to_guest']
        channel.send_ctrl_msg(self.client_socket, eventfd.fileno(), 0)

        glog.debug('server sending shm fd %d' % self.shmfd)
        channel.send_ctrl_msg(self.client_socket, self.shmfd, 0)
        glog.info('server closing connection')
        self.client_socket.close()
