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

//! Core library shared by the emulated camera vhost-user-media backends.
//!
//! This crate contains the virtio-media device implementation
//! ([`device::EmulatedCamera`]), the frame generation pipeline
//! ([`capture_channel`]) and the test patterns ([`pattern`]), together with
//! [`serve`], the daemon loop that binaries use to expose the device over a
//! vhost-user socket.

use std::path::Path;
use std::sync::{Arc, RwLock};

use thiserror::Error;
use vhost_user_backend::VhostUserDaemon;
use vhu_media::{HostMemoryMapper, VhuMediaBackend};
use virtio_media::protocol::VirtioMediaDeviceConfig;
use virtio_media::v4l2r::ioctl::Capabilities;
use vm_memory::{GuestMemoryAtomic, GuestMemoryLoadGuard, GuestMemoryMmap};

pub mod capture_channel;
pub mod control;
pub mod controls;
pub mod device;
pub mod format;
pub mod isp;
pub mod memory;
pub mod pattern;
pub mod pipeline;
pub mod render_params;
pub mod scene;
pub mod sensor;

pub use capture_channel::CaptureSink;
pub use control::{Control, ControlDescriptor, ControlSet};
pub use controls::LensFacing;
pub use device::{EmulatedCamera, FrameSource};
pub use format::Yuv420m;
pub use memory::{BufferMemoryHandler, QbufRequest, QueuedBuffer};
pub use pipeline::Pipeline;
pub use render_params::{Gain, RenderParams, TestPattern};

/// Errors returned by [`serve`].
#[derive(Debug, Error)]
pub enum Error {
    #[error("Could not create daemon: {0}")]
    CouldNotCreateDaemon(vhost_user_backend::Error),
    #[error("Fatal error: {0}")]
    ServeFailed(vhost_user_backend::Error),
}

pub type Result<T> = std::result::Result<T, Error>;

/// `VFL_TYPE_VIDEO` from the Linux V4L2 UAPI.
const VFL_TYPE_VIDEO: u32 = 0;

/// Builds the virtio-media device configuration advertised to the guest.
fn device_config(card_name: &str) -> VirtioMediaDeviceConfig {
    let mut card = [0u8; 32];
    let len = card_name.len().min(card.len());
    card[..len].copy_from_slice(&card_name.as_bytes()[..len]);
    VirtioMediaDeviceConfig {
        device_caps: (Capabilities::VIDEO_CAPTURE_MPLANE | Capabilities::STREAMING).bits(),
        device_type: VFL_TYPE_VIDEO,
        card,
    }
}

/// Serves an [`EmulatedCamera`] over the vhost-user socket at `socket_path`.
///
/// `card_name` is reported to the guest through `VIDIOC_QUERYCAP`. When the main VM shuts down
/// the daemon exits gracefully; this function then re-creates it so that the backend survives
/// VM restarts without having to be started again manually. It therefore only returns on error.
pub fn serve<M, C, F>(
    socket_path: &Path,
    card_name: &str,
    frame_source: FrameSource,
    create_control_set: C,
    create_buffer_memory_handler: F,
) -> Result<()>
where
    M: BufferMemoryHandler,
    C: Fn() -> ControlSet + Clone + Send + Sync + 'static,
    F: Fn(HostMemoryMapper, GuestMemoryLoadGuard<GuestMemoryMmap>) -> M
        + Clone
        + Send
        + Sync
        + 'static,
{
    loop {
        let create_control_set = create_control_set.clone();
        let create_buffer_memory_handler = create_buffer_memory_handler.clone();
        let backend = Arc::new(RwLock::new(VhuMediaBackend::new(
            device_config(card_name),
            move |event_queue, host_mapper, guest_mem| {
                EmulatedCamera::new(
                    event_queue,
                    create_buffer_memory_handler(host_mapper, guest_mem),
                    create_control_set(),
                    frame_source,
                )
            },
        )));
        let mut daemon = VhostUserDaemon::new(
            String::from("vhost-user-media-backend"),
            backend,
            GuestMemoryAtomic::new(GuestMemoryMmap::new()),
        )
        .map_err(Error::CouldNotCreateDaemon)?;
        log::info!("vhost-user-media-backend daemon started");
        daemon.serve(socket_path).map_err(Error::ServeFailed)?;
        log::info!("vhost-user-media-backend daemon closed gracefully");
    }
}
