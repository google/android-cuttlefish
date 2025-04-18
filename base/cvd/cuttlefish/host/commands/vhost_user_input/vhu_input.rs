use std::fs::File;
use std::io::{
    stdout, Error as IoError, ErrorKind as IoErrorKind, Read, Result as IoResult, Write,
};

use anyhow::{bail, Context, Result};
use log::{error, trace};
use vhost::vhost_user::message::{
    VhostTransferStateDirection, VhostTransferStatePhase, VhostUserProtocolFeatures,
    VhostUserVirtioFeatures,
};
use vhost_user_backend::{
    VhostUserBackend, VhostUserBackendMut, VringEpollHandler, VringRwLock, VringT,
};
use virtio_bindings::bindings::{
    virtio_config::VIRTIO_F_NOTIFY_ON_EMPTY, virtio_config::VIRTIO_F_VERSION_1,
    virtio_ring::VIRTIO_RING_F_EVENT_IDX,
};
use virtio_queue::QueueOwnedT;
use vm_memory::{GuestAddressSpace, GuestMemoryAtomic, GuestMemoryMmap};
use vmm_sys_util::epoll::EventSet;

use crate::buf_reader::BufReader;
use crate::vio_input::{trim_to_event_size_multiple, VirtioInputConfig};

const VIRTIO_INPUT_NUM_QUEUES: usize = 2;
const VIRTIO_INPUT_MAX_QUEUE_SIZE: usize = 256;
const FEATURES: u64 = 1 << VIRTIO_F_VERSION_1
    | 1 << VIRTIO_F_NOTIFY_ON_EMPTY
    | 1 << VIRTIO_RING_F_EVENT_IDX
    | VhostUserVirtioFeatures::PROTOCOL_FEATURES.bits();

const EVENT_QUEUE: u16 = 0;
const STATUS_QUEUE: u16 = 1;
const EXIT_EVENT: u16 = 2;
const STDIN_EVENT: u16 = 3;

/// Vhost-user input backend implementation.
#[derive(Clone)]
pub struct VhostUserInput<R: Read + Sync + Send> {
    config: VirtioInputConfig,
    event_reader: BufReader<R>,
    event_idx: bool,
    mem: Option<GuestMemoryAtomic<GuestMemoryMmap>>,
}

impl<R: Read + Sync + Send> VhostUserBackendMut for VhostUserInput<R> {
    type Bitmap = ();
    type Vring = VringRwLock;
    fn num_queues(&self) -> usize {
        trace!("num_queues");
        VIRTIO_INPUT_NUM_QUEUES
    }

    fn max_queue_size(&self) -> usize {
        trace!("max_queue_size");
        VIRTIO_INPUT_MAX_QUEUE_SIZE
    }

    fn features(&self) -> u64 {
        trace!("features");
        FEATURES
    }

    fn protocol_features(&self) -> VhostUserProtocolFeatures {
        trace!("protocol_features");
        VhostUserProtocolFeatures::CONFIG | VhostUserProtocolFeatures::DEVICE_STATE
    }

    fn set_event_idx(&mut self, enabled: bool) {
        trace!("set_event_idx: {}", enabled);
        self.event_idx = enabled;
    }

    fn update_memory(
        &mut self,
        mem: GuestMemoryAtomic<GuestMemoryMmap<Self::Bitmap>>,
    ) -> IoResult<()> {
        trace!("update_memory");
        self.mem = Some(mem);
        Ok(())
    }

    fn exit_event(&self, thread_index: usize) -> Option<vmm_sys_util::eventfd::EventFd> {
        trace!("exit_event: thread_idx={}", thread_index);
        vmm_sys_util::eventfd::EventFd::new(0).ok()
    }

    fn queues_per_thread(&self) -> Vec<u64> {
        // Handle all queues in the same thread since only one queue has frequent activity.
        vec![0xffff_ffff]
    }

    fn set_device_state_fd(
        &mut self,
        direction: VhostTransferStateDirection,
        _phase: VhostTransferStatePhase,
        _file: File,
    ) -> IoResult<Option<File>> {
        trace!("set_device_state_fd: direction={:?}", direction);
        Ok(None)
    }

    fn check_device_state(&self) -> IoResult<()> {
        trace!("check_device_state");
        Ok(())
    }

    fn get_config(&self, offset: u32, size: u32) -> Vec<u8> {
        trace!("get_config: offset={}, size={}", offset, size);
        match self.config.get_raw() {
            Ok(raw_config) => raw_config[offset as usize..(offset + size) as usize].to_vec(),
            Err(e) => {
                error!("Failed to get valid config: {:?}", e);
                vec![0u8; size as usize]
            }
        }
    }

    fn set_config(&mut self, offset: u32, buf: &[u8]) -> IoResult<()> {
        trace!("set_config: offset: {}, values: {:?}", offset, buf);
        self.config.set_raw(offset, buf).map_err(|e| IoError::new(IoErrorKind::InvalidInput, e))
    }

    fn handle_event(
        &mut self,
        device_event: u16,
        _evset: EventSet,
        vrings: &[Self::Vring],
        _thread_id: usize,
    ) -> IoResult<()> {
        match device_event {
            EVENT_QUEUE => {
                trace!("event queue event");
                self.send_pending_events(&vrings[EVENT_QUEUE as usize]).map_err(IoError::other)?;
            }
            STATUS_QUEUE => {
                trace!("status queue event");
                self.write_status_updates(&vrings[STATUS_QUEUE as usize])
                    .map_err(IoError::other)?;
            }
            EXIT_EVENT => {
                trace!("Exit event");
            }
            STDIN_EVENT => {
                trace!("Stdin event");
                self.read_input_events().map_err(IoError::other)?;
                self.send_pending_events(&vrings[EVENT_QUEUE as usize]).map_err(IoError::other)?;
            }
            _ => {
                error!("Unknown device event: {}", device_event);
            }
        }
        Ok(())
    }
}

impl<R: Read + Sync + Send> VhostUserInput<R> {
    /// Construct a new VhostUserInput backend.
    pub fn new(device_config: VirtioInputConfig, reader: R) -> VhostUserInput<R> {
        VhostUserInput {
            config: device_config,
            event_reader: BufReader::new(reader),
            event_idx: false,
            mem: None,
        }
    }

    fn send_pending_events(&mut self, vring: &VringRwLock) -> Result<()> {
        // Only if can send at least one full event
        if trim_to_event_size_multiple(self.event_reader.buffer().len()) == 0 {
            return Ok(());
        }
        let mut vring_state = vring.get_mut();
        let Some(atomic_mem) = &self.mem else {
            bail!("Guest memory not available");
        };
        while let Some(avail_desc) = vring_state
            .get_queue_mut()
            .iter(atomic_mem.memory())
            .context("Failed to iterate over queue descriptors")?
            .next()
        {
            let mem = atomic_mem.memory();
            let head_index = avail_desc.head_index();
            let mut writer = avail_desc.writer(&mem).context("Failed to get writable buffers")?;
            let mut write_len =
                std::cmp::min(self.event_reader.buffer().len(), writer.available_bytes());
            // Send only full events
            write_len = trim_to_event_size_multiple(write_len);
            writer.write_all(&self.event_reader.buffer()[..write_len])?;
            self.event_reader.consume(write_len);

            vring_state
                .add_used(head_index, write_len as u32)
                .context("Couldn't return used descriptor to the ring")?;

            if trim_to_event_size_multiple(self.event_reader.buffer().len()) == 0 {
                // No more events available
                break;
            }
        }
        let needs_notification = !self.event_idx
            || match vring_state.needs_notification() {
                Ok(v) => v,
                Err(e) => {
                    error!("Couldn't check if vring needs notification: {:?}", e);
                    true
                }
            };
        if needs_notification {
            vring_state.signal_used_queue().unwrap();
        }
        Ok(())
    }

    fn read_input_events(&mut self) -> Result<()> {
        self.event_reader.read_ahead()?;
        Ok(())
    }

    fn write_status_updates(&mut self, vring: &VringRwLock) -> Result<()> {
        let mut vring_state = vring.get_mut();
        let Some(atomic_mem) = &self.mem else {
            bail!("Guest memory not available");
        };
        while let Some(avail_desc) = vring_state
            .get_queue_mut()
            .iter(atomic_mem.memory())
            .context("Failed to iterate over queue descriptors")?
            .next()
        {
            let mem = atomic_mem.memory();
            let head_index = avail_desc.head_index();
            let mut reader = avail_desc.reader(&mem).context("Failed to get readable buffers")?;
            let bytes = reader.available_bytes();
            let mut buf = vec![0u8; bytes];
            reader.read_exact(&mut buf)?;
            stdout().write_all(&buf)?;

            vring_state
                .add_used(head_index, bytes as u32)
                .context("Couldn't return used descriptor to the ring")?;
        }
        let needs_notification = !self.event_idx
            || match vring_state.needs_notification() {
                Ok(v) => v,
                Err(e) => {
                    error!("Couldn't check if vring needs notification: {:?}", e);
                    true
                }
            };
        if needs_notification {
            vring_state.signal_used_queue().unwrap();
        }
        Ok(())
    }

    pub fn register_handlers<T: VhostUserBackend>(
        fd: i32,
        handler: &VringEpollHandler<T>,
    ) -> IoResult<()> {
        trace!("register_handlers");
        handler.register_listener(fd, vmm_sys_util::epoll::EventSet::IN, STDIN_EVENT as u64)
    }
}
