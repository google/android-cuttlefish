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

use v4l2r::ioctl::V4l2Buffer;
use v4l2r::memory::MemoryType;
use virtio_media::ioctl::IoctlResult;
use virtio_media::protocol::SgEntry;

use crate::capture_channel::CaptureSink;
use crate::format::Yuv420m;

/// What the guest passed on `QBUF`.
pub struct QbufRequest {
    pub buffer: V4l2Buffer,
    /// Guest memory of each plane, only set for `V4L2_MEMORY_USERPTR` buffers.
    pub guest_regions: Vec<Vec<SgEntry>>,
}

/// What a `QBUF` command resolves to.
pub struct QueuedBuffer {
    /// Buffer returned to the guest on `DQBUF`.
    pub buffer: V4l2Buffer,
    /// Where the frame is written.
    pub sink: Box<dyn CaptureSink>,
}

/// Handles the memory of the capture buffers for one `V4L2_MEMORY_*` type.
pub trait BufferMemoryHandler: Send + Sync + 'static {
    /// The `V4L2_MEMORY_*` type handled.
    const MEMORY_TYPE: MemoryType;

    type Allocation: Send;

    /// Allocates the memory of the planes (`REQBUFS`).
    ///
    /// `v4l2_buffer` already has its queue, index, memory type and plane lengths set;
    /// implementations fill in the memory specific plane fields.
    fn alloc(
        &mut self,
        v4l2_buffer: &mut V4l2Buffer,
        plane_sizes: [u32; Yuv420m::NUM_PLANES],
    ) -> IoctlResult<Self::Allocation>;

    /// Releases the memory of the planes (`REQBUFS(0)`, session close).
    fn free(&mut self, allocation: &Self::Allocation);

    /// Resolves where the frame is written (`QBUF`).
    fn queue(
        &mut self,
        allocation: &Self::Allocation,
        querybuf_response: V4l2Buffer,
        qbuf: QbufRequest,
        plane_sizes: [u32; Yuv420m::NUM_PLANES],
    ) -> IoctlResult<QueuedBuffer>;

    /// Handles the guest `mmap()` calls.
    fn mmap<'a>(
        &mut self,
        allocations: impl Iterator<Item = &'a Self::Allocation>,
        flags: u32,
        offset: u32,
    ) -> IoctlResult<(u64, u64)>
    where
        Self::Allocation: 'a;

    /// Handles the guest `munmap()` calls.
    fn munmap(&mut self, guest_addr: u64) -> IoctlResult<()>;
}
