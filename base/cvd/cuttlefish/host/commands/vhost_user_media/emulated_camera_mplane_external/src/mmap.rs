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

//! `V4L2_MEMORY_MMAP` buffers: device-owned memfds, one per plane, exposed to the guest through
//! the virtio-media shared memory region.

use std::fs::File;
use std::io::{Seek, Write};
use std::os::fd::AsFd;

use emulated_camera::{BufferMemoryHandler, CaptureSink, QbufRequest, QueuedBuffer, Yuv420m};
use v4l2r::ioctl::{V4l2Buffer, V4l2PlanesWithBackingMut};
use v4l2r::memory::MemoryType;
use vhu_media::HostMemoryMapper;
use virtio_media::ioctl::IoctlResult;
use virtio_media::memfd::MemFdBuffer;
use virtio_media::mmap::MmapMappingManager;
use virtio_media::protocol::VIRTIO_MEDIA_MMAP_FLAG_RW;

/// One plane of an MMAP buffer.
pub struct Plane {
    /// Backing memory file descriptor.
    fd: MemFdBuffer,
    /// Offset the guest passes to `mmap()` to map this plane.
    offset: u32,
}

/// The memfds of a buffer, each rewound before every frame.
struct MemFdPlanes([File; Yuv420m::NUM_PLANES]);

impl CaptureSink for MemFdPlanes {
    fn planes(&mut self) -> std::io::Result<[&mut dyn Write; Yuv420m::NUM_PLANES]> {
        for file in &mut self.0 {
            file.rewind()?;
        }
        let [y, u, v] = &mut self.0;
        Ok([y, u, v])
    }
}

/// [`BufferMemoryHandler`] for `V4L2_MEMORY_MMAP` buffers.
pub struct MmapHandler {
    mmap_manager: MmapMappingManager<HostMemoryMapper>,
}

impl MmapHandler {
    /// `mapper` exposes the memfds to the guest through the virtio-media shared memory region.
    pub fn new(mapper: HostMemoryMapper) -> Self {
        Self {
            mmap_manager: MmapMappingManager::from(mapper),
        }
    }
}

impl BufferMemoryHandler for MmapHandler {
    const MEMORY_TYPE: MemoryType = MemoryType::Mmap;

    type Allocation = [Plane; Yuv420m::NUM_PLANES];

    fn alloc(
        &mut self,
        v4l2_buffer: &mut V4l2Buffer,
        plane_sizes: [u32; Yuv420m::NUM_PLANES],
    ) -> IoctlResult<Self::Allocation> {
        let mut planes = Vec::with_capacity(Yuv420m::NUM_PLANES);
        for (i, size) in plane_sizes.into_iter().enumerate() {
            let fd = MemFdBuffer::new(size as u64).map_err(|e| {
                log::error!("failed to allocate MMAP buffer plane {}: {:#}", i, e);
                libc::ENOMEM
            })?;
            // `None` lets the manager pick the offset.
            let offset = self
                .mmap_manager
                .register_buffer(/* offset= */ None, size)
                .map_err(|_| libc::EINVAL)?;
            planes.push(Plane { fd, offset });
        }
        let planes: Self::Allocation = planes
            .try_into()
            .unwrap_or_else(|_| unreachable!("exactly {} planes pushed", Yuv420m::NUM_PLANES));

        let V4l2PlanesWithBackingMut::Mmap(v4l2_planes) =
            v4l2_buffer.planes_with_backing_iter_mut()
        else {
            unreachable!("buffer memory type is not MMAP")
        };
        for (mut v4l2_plane, plane) in v4l2_planes.zip(&planes) {
            v4l2_plane.set_mem_offset(plane.offset);
        }

        Ok(planes)
    }

    fn free(&mut self, planes: &Self::Allocation) {
        for plane in planes {
            self.mmap_manager.unregister_buffer(plane.offset);
        }
    }

    fn queue(
        &mut self,
        planes: &Self::Allocation,
        querybuf_response: V4l2Buffer,
        _qbuf: QbufRequest,
        _plane_sizes: [u32; Yuv420m::NUM_PLANES],
    ) -> IoctlResult<QueuedBuffer> {
        let mut files = Vec::with_capacity(Yuv420m::NUM_PLANES);
        for plane in planes {
            files.push(plane.fd.as_file().try_clone().map_err(|_| libc::EIO)?);
        }
        let files: [File; Yuv420m::NUM_PLANES] = files
            .try_into()
            .unwrap_or_else(|_| unreachable!("exactly {} planes cloned", Yuv420m::NUM_PLANES));
        Ok(QueuedBuffer {
            buffer: querybuf_response,
            sink: Box::new(MemFdPlanes(files)),
        })
    }

    fn mmap<'a>(
        &mut self,
        allocations: impl Iterator<Item = &'a Self::Allocation>,
        flags: u32,
        offset: u32,
    ) -> IoctlResult<(u64, u64)> {
        let plane = allocations
            .flatten()
            .find(|p| p.offset == offset)
            .ok_or(libc::EINVAL)?;
        let rw = (flags & VIRTIO_MEDIA_MMAP_FLAG_RW) != 0;
        self.mmap_manager
            .create_mapping(offset, plane.fd.as_file().as_fd(), rw)
            .map_err(|_| libc::EINVAL)
    }

    fn munmap(&mut self, guest_addr: u64) -> IoctlResult<()> {
        self.mmap_manager
            .remove_mapping(guest_addr)
            .map(|_| ())
            .map_err(|_| libc::EINVAL)
    }
}
