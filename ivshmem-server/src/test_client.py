import array
import channel
import struct
import socket
import os


def recv_fd(conn):
  msg, ancdata, flags, addr = conn.recvmsg(1, socket.CMSG_LEN(struct.calcsize('i')))
  cmsg_level, cmsg_type, cmsg_data = ancdata[0]
  fda = array.array('I')
  fda.frombytes(cmsg_data)
  return fda[0]

def startclient(client_name, socket_path):
  conn = channel.connect_to_channel(socket_path)
  print('client sending: GET PROTOCOL_VER')
  channel.send_msg_utf8(conn, 'GET PROTOCOL_VER')
  proto_ver = channel.recv_msg(conn,len('0xAABBCCDD'))
  print('client got: %s' % proto_ver)
  region_name_str_len = "0x%08x" % len(client_name)

  print('client sending: ' + 'INFORM REGION_NAME_LEN: %s' % region_name_str_len)
  channel.send_msg_utf8(conn, 'INFORM REGION_NAME_LEN: %s' % region_name_str_len)

  print('client sending: ' + client_name)
  channel.send_msg_utf8(conn, client_name)

  region_start_offset = channel.recv_msg(conn, len('0xAABBCCDD'))
  print('client got region_start_offset: %s' % region_start_offset)

  region_end_offset = channel.recv_msg(conn, len('0xAABBCCDD'))
  print('client got region_end_offset: %s' % region_end_offset)

  print('client waiting for node count')
  node_count = channel.recv_msg(conn, len('0xAABBCCDD'))
  print("node count %s" % node_count)
  for node in range(int(node_count, base=16)):
     print('TODO: client waiting for lock offsets for node %d' % node)

  print('client waiting for shm descriptor')
  fd = recv_fd(conn)
  print('shm size is %d MB' % (os.fstat(fd)[6] >> 20))

  #print('client waiting for eventfds (one per node in the region)')

  #eventfds=[]
  #for count in range(int(region_count)):
  #  eventfds.append(recv_fd(conn))
  #print(eventfds)
  print('client waiting for lock offsets (one per node)')

if __name__ == '__main__':
  startclient('HWCOMPOSER', '/tmp/ivshmem_socket_client')

