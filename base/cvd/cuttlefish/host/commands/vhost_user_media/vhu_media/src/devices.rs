// Copyright 2026, The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

use v4l2r::QueueType;
use v4l2r::bindings;
use v4l2r::bindings::v4l2_fmtdesc;
use v4l2r::bindings::v4l2_format;
use v4l2r::ioctl::BufferCapabilities;
use v4l2r::ioctl::BufferField;
use v4l2r::ioctl::BufferFlags;
use v4l2r::ioctl::V4l2Buffer;
use v4l2r::ioctl::V4l2PlanesWithBackingMut;
use v4l2r::memory::MemoryType;

use virtio_media::ioctl::IoctlResult;
use virtio_media::memfd::MemFdBuffer;


#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum BufferState {
    New,
    Incoming,
    Outgoing { sequence: u32 },
}

pub struct Buffer {
    pub state: BufferState,
    pub v4l2_buffer: V4l2Buffer,
    pub fd: MemFdBuffer,
    pub offset: u32,
}

impl Buffer {
    pub fn new(v4l2_buffer: V4l2Buffer, fd: MemFdBuffer, offset: u32) -> Self {
        Self {
            state: BufferState::New,
            v4l2_buffer,
            fd,
            offset,
        }
    }

    pub fn set_state(&mut self, state: BufferState, buffer_size: u32) {
        let mut flags = self.v4l2_buffer.flags();
        match state {
            BufferState::New => {
                *self.v4l2_buffer.get_first_plane_mut().bytesused = 0;
                flags &= !BufferFlags::QUEUED;
            }
            BufferState::Incoming => {
                *self.v4l2_buffer.get_first_plane_mut().bytesused = 0;
                flags |= BufferFlags::QUEUED;
            }
            BufferState::Outgoing { sequence } => {
                *self.v4l2_buffer.get_first_plane_mut().bytesused = buffer_size;
                self.v4l2_buffer.set_sequence(sequence);
                self.v4l2_buffer.set_timestamp(bindings::timeval {
                    tv_sec: (sequence + 1) as bindings::__time_t / 1000,
                    tv_usec: (sequence + 1) as bindings::__time_t % 1000,
                });
                flags &= !BufferFlags::QUEUED;
            }
        }
        self.v4l2_buffer.set_flags(flags);
        self.state = state;
    }
}

pub fn set_plane_offset_and_length(v4l2_buffer: &mut V4l2Buffer, offset: u32, buffer_size: u32) {
    if let V4l2PlanesWithBackingMut::Mmap(mut planes) =
        v4l2_buffer.planes_with_backing_iter_mut()
    {
        // SAFETY: every buffer has at least one plane.
        let mut plane = planes.next().unwrap();
        plane.set_mem_offset(offset);
        *plane.length = buffer_size;
    } else {
        // SAFETY: we have just set the buffer type to MMAP. Reaching this point means a bug in
        // the code.
        panic!()
    }
}
