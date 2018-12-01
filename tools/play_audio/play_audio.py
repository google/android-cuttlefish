#!/usr/bin/env python3
#
# Copyright (C) 2018 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0(the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
"""Streams audio from running cuttlefish instance.

I'm seeing a bunch of errors/warnings from ALSA lib, in particular:

"ALSA lib pcm.c:8424:(snd_pcm_recover) underrun occurred"
whenever audio stops or lags. I haven't seen any actual problems as a result.

You may see some "could not connect" messages at first before the ssh tunnel
is established.

sudo apt install python3-pyaudio

"""

import argparse
import collections
import socket
import struct
import subprocess
import sys
import threading
import time
from typing import List

import pyaudio

import gcloud_config

AudioDescription = collections.namedtuple(
    'AudioDescription', ['sample_width', 'num_channels', 'frame_rate'])

def recvall(sock: socket.socket, num_bytes: int) -> bytes:
  received = b''
  while len(received) < num_bytes:
    just_received = sock.recv(num_bytes - len(received))
    if not just_received:
      break
    received += just_received
  return received


def open_stream(desc: AudioDescription) -> pyaudio.Stream:
  p = pyaudio.PyAudio()
  return p.open(
      format=p.get_format_from_width(desc.sample_width),
      channels=desc.num_channels,
      rate=desc.frame_rate,
      frames_per_buffer=3840//desc.num_channels//desc.sample_width,
      output=True)


def receive_audio_description(conn: socket.socket) -> AudioDescription:
  raw_header = recvall(conn, 6)
  return AudioDescription(*struct.unpack('!HHH', raw_header))


def receive_length(conn: socket.socket) -> int:
  return struct.unpack('!I', recvall(conn, 4))[0]



def launch_ssh_tunnel_thread(
    instance: str, project: str, zone: str, port: int) -> threading.Thread:
  cmd = ['gcloud', 'compute', 'ssh', gcloud_config.project_flag(project),
         gcloud_config.zone_flag(zone), 'vsoc-01@{}'.format(instance),
         '--' , '-L{0}:127.0.0.1:{0}'.format(port), '-N']
  t = threading.Thread(target=subprocess.check_call, args=(cmd,), daemon=True)
  t.start()
  return t

def main(instance: str, project: str, zone: str, device_num: int) -> None:
  BASE_AUDIO_PORT = 7444
  port = 7444 + device_num - 1
  tunnel_thread = launch_ssh_tunnel_thread(instance, project, zone, port)
  conn = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
  while True:
    try:
      conn.connect(('localhost', port))
    except ConnectionRefusedError:
      if not tunnel_thread.is_alive():
        print('Failed to set up ssh tunnel')
        sys.exit(1)
      print('Could not connect, retrying')
      time.sleep(1)
    else:
      break

  description = receive_audio_description(conn)
  stream = open_stream(description)

  while True:
    payload_size = receive_length(conn)
    data = recvall(conn, payload_size)
    if len(data) != payload_size:
      print('received', len(data), file=sys.stderr)
      break
    stream.write(data)


if __name__ == '__main__':
  parser = argparse.ArgumentParser()
  parser.add_argument('instance', help='name of gce instance')
  parser.add_argument('--project', default=gcloud_config.project(),
                      help='name of gce project')
  parser.add_argument('--zone', default=gcloud_config.zone(),
                      help='gce zone')
  parser.add_argument('--device_num', type=int, default=1,
                      help='device number corresponding to remote vsoc-## user name')
  args = parser.parse_args()
  main(**vars(args))

