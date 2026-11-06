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

//! `V4L2_MEMORY_USERPTR` buffers: guest-owned memory whose location arrives with every `QBUF`
//! as an sg-list, written to directly by the device.

use std::io::Write;

use emulated_camera::{BufferMemoryHandler, CaptureSink, QbufRequest, QueuedBuffer, Yuv420m};
use v4l2r::ioctl::{V4l2Buffer, V4l2PlanesWithBacking, V4l2PlanesWithBackingMut};
use v4l2r::memory::MemoryType;
use virtio_media::ioctl::IoctlResult;
use virtio_media::protocol::SgEntry;
use vm_memory::{Bytes, GuestAddress, GuestMemoryLoadGuard, GuestMemoryMmap};

/// One plane of a USERPTR buffer: the guest memory regions the driver attached to `QBUF`, exposed
/// as a sequential [`Write`] sink for the pattern generators.
///
/// The driver serialises the `QBUF` sg-list into a 16 KiB buffer, 16 bytes per region: at most
/// ~1,000 regions, i.e. ≈4 MiB in the worst case of guest memory scattered over 4 KiB pages. See
/// https://github.com/chromeos/virtio-media/blob/9acaa53736eafd40deb98213200feba84e57db14/driver/session.h#L24
struct GuestPlane {
    regions: Vec<SgEntry>,
    mem: GuestMemoryLoadGuard<GuestMemoryMmap>,
    /// Index of the region currently being written.
    region_idx: usize,
    /// Bytes already written into `regions[region_idx]`.
    region_pos: u64,
}

impl GuestPlane {
    fn new(regions: Vec<SgEntry>, mem: GuestMemoryLoadGuard<GuestMemoryMmap>) -> Self {
        Self {
            regions,
            mem,
            region_idx: 0,
            region_pos: 0,
        }
    }

    /// Total number of bytes addressable through `regions`.
    fn len(&self) -> u64 {
        self.regions.iter().map(|r| r.len as u64).sum()
    }

    fn rewind(&mut self) {
        self.region_idx = 0;
        self.region_pos = 0;
    }
}

impl Write for GuestPlane {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        let mut written = 0;
        while written < buf.len() {
            let Some(region) = self.regions.get(self.region_idx) else {
                return Err(std::io::Error::new(
                    std::io::ErrorKind::WriteZero,
                    "guest buffer too small for frame",
                ));
            };
            let remaining = region.len as u64 - self.region_pos;
            if remaining == 0 {
                self.region_idx += 1;
                self.region_pos = 0;
                continue;
            }
            let chunk = std::cmp::min(remaining as usize, buf.len() - written);
            let addr = GuestAddress(region.start + self.region_pos);
            self.mem
                .write_slice(&buf[written..written + chunk], addr)
                .map_err(std::io::Error::other)?;
            self.region_pos += chunk as u64;
            written += chunk;
        }
        Ok(written)
    }

    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

/// The guest planes of a `QBUF`.
struct GuestPlanes([GuestPlane; Yuv420m::NUM_PLANES]);

impl CaptureSink for GuestPlanes {
    fn planes(&mut self) -> std::io::Result<[&mut dyn Write; Yuv420m::NUM_PLANES]> {
        for plane in &mut self.0 {
            plane.rewind();
        }
        let [y, u, v] = &mut self.0;
        Ok([y, u, v])
    }
}

/// [`BufferMemoryHandler`] for `V4L2_MEMORY_USERPTR` buffers.
///
/// Nothing is allocated on the device side: the guest supplies the storage with each `QBUF`.
pub struct UserPtrHandler {
    guest_mem: GuestMemoryLoadGuard<GuestMemoryMmap>,
}

impl UserPtrHandler {
    pub fn new(guest_mem: GuestMemoryLoadGuard<GuestMemoryMmap>) -> Self {
        Self { guest_mem }
    }
}

impl BufferMemoryHandler for UserPtrHandler {
    const MEMORY_TYPE: MemoryType = MemoryType::UserPtr;

    type Allocation = ();

    fn alloc(
        &mut self,
        _v4l2_buffer: &mut V4l2Buffer,
        _plane_sizes: [u32; Yuv420m::NUM_PLANES],
    ) -> IoctlResult<Self::Allocation> {
        Ok(())
    }

    fn free(&mut self, _allocation: &Self::Allocation) {}

    fn queue(
        &mut self,
        _allocation: &Self::Allocation,
        querybuf_response: V4l2Buffer,
        qbuf: QbufRequest,
        plane_sizes: [u32; Yuv420m::NUM_PLANES],
    ) -> IoctlResult<QueuedBuffer> {
        // Echo the guest's `userptr` and `length` back on `DQBUF`; the device owns everything
        // else (state, flags, timing).
        let mut dqbuf = querybuf_response;
        let V4l2PlanesWithBacking::UserPtr(qbuf_planes) = qbuf.buffer.planes_with_backing_iter()
        else {
            unreachable!("queued buffer memory type is not USERPTR")
        };
        let V4l2PlanesWithBackingMut::UserPtr(dqbuf_planes) = dqbuf.planes_with_backing_iter_mut()
        else {
            unreachable!("querybuf response memory type is not USERPTR")
        };
        for (qbuf_plane, mut dqbuf_plane) in qbuf_planes.zip(dqbuf_planes) {
            dqbuf_plane.set_userptr(qbuf_plane.userptr());
            *dqbuf_plane.length = *qbuf_plane.length;
        }

        let regions: [Vec<SgEntry>; Yuv420m::NUM_PLANES] =
            qbuf.guest_regions.try_into().map_err(|regions: Vec<_>| {
                log::error!(
                    "qbuf: expected {} USERPTR planes, got {}",
                    Yuv420m::NUM_PLANES,
                    regions.len()
                );
                libc::EINVAL
            })?;

        let mut planes = Vec::with_capacity(Yuv420m::NUM_PLANES);
        for (regions, size) in regions.into_iter().zip(plane_sizes) {
            let plane = GuestPlane::new(regions, self.guest_mem.clone());
            if plane.len() < size as u64 {
                log::error!(
                    "qbuf: USERPTR plane of {} bytes is smaller than {} required",
                    plane.len(),
                    size
                );
                return Err(libc::EINVAL);
            }
            planes.push(plane);
        }
        let planes: [GuestPlane; Yuv420m::NUM_PLANES] = planes
            .try_into()
            .unwrap_or_else(|_| unreachable!("exactly {} planes pushed", Yuv420m::NUM_PLANES));

        Ok(QueuedBuffer {
            buffer: dqbuf,
            sink: Box::new(GuestPlanes(planes)),
        })
    }

    fn mmap<'a>(
        &mut self,
        _allocations: impl Iterator<Item = &'a Self::Allocation>,
        _flags: u32,
        _offset: u32,
    ) -> IoctlResult<(u64, u64)> {
        Err(libc::EINVAL)
    }

    fn munmap(&mut self, _guest_addr: u64) -> IoctlResult<()> {
        Err(libc::EINVAL)
    }
}
