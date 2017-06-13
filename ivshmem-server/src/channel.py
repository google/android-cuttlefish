'''
  Related to the UNIX Domain Socket.
'''

import fcntl
import socket
import struct

# From include/uapi/asm-generic/ioctls.h
# #define FIONBIO 0x5421
_FIONBIO = 0x5421

def start_listener(path):
  uds = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
  uds.bind(path)
  uds.listen(5)
  return uds


def connect_to_channel(path):
  uds = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
  uds.connect(path)
  return uds


def handle_new_connection(uds, nonblocking=True):
  sock, addr = uds.accept()

  if nonblocking:
    # Set the new socket to non-blocking mode.
    try:
      fcntl.ioctl(sock.fileno(), _FIONBIO, struct.pack('L', 1))
    except OSError as e:
      print('Failed in changing the socket to Non-blocking mode ' + e)
  return sock

#
# Send 8 bytes.
#
def send_msg_8(sock, data):
  sock.sendmsg([bytes(struct.pack('q', data))])

def send_ctrl_msg(sock, fd, quad_data):
  print(sock, fd)
  sock.sendmsg([bytes(struct.pack('q', quad_data))],
               [(socket.SOL_SOCKET, socket.SCM_RIGHTS,
                 bytes(struct.pack('i', fd)))])


def send_msg_utf8(sock, data):
  sock.sendall(bytes(data, encoding='utf-8'))


def recv_msg(sock, expected_length):
  chunks = []
  received = 0
  while received < expected_length:
    part = sock.recv(expected_length - received)
    received += len(part)
    chunks.append(part.decode(encoding='utf-8'))
  data = ''.join(chunks)
  return data
