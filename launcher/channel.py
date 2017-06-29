'''
    Related to the UNIX Domain Socket.
'''

import fcntl
import os
import socket
import stat
import struct
import glog

# From include/uapi/asm-generic/ioctls.h
# #define FIONBIO 0x5421
_FIONBIO = 0x5421


def start_listener(path):
    uds = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    uds.bind(path)
    uds.listen(5)
    os.chmod(path,
             stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR |
             stat.S_IRGRP | stat.S_IWGRP | stat.S_IXGRP |
             stat.S_IROTH | stat.S_IWOTH | stat.S_IXOTH)
    glog.info('Socket %s ready.' % path)
    return uds


def connect_to_channel(path):
    uds = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    uds.connect(path)
    return uds


def handle_new_connection(uds, nonblocking=True):
    sock, _ = uds.accept()

    if nonblocking:
        # Set the new socket to non-blocking mode.
        try:
            fcntl.ioctl(sock.fileno(), _FIONBIO, struct.pack('L', 1))
        except OSError as exc:
            glog.exception(
                'Exception caught while trying to make client nonblocking: ', exc)
    return sock


def send_msg_8(sock, data):
    """Send 8 bytes of data over the socket.
    """
    sock.sendmsg([bytes(struct.pack('q', data))])


def send_ctrl_msg(sock, fd, quad_data):
    sock.sendmsg([bytes(struct.pack('q', quad_data))],
                 [(socket.SOL_SOCKET, socket.SCM_RIGHTS,
                   bytes(struct.pack('i', fd)))])


def send_msg_utf8(sock, data):
    sock.sendall(bytes(data, encoding='ascii'))


def recv_msg(sock, expected_length):
    chunks = []
    received = 0
    while received < expected_length:
        part = sock.recv(expected_length - received)
        received += len(part)
        chunks.append(part.decode(encoding='ascii'))
    data = ''.join(chunks)
    return data
