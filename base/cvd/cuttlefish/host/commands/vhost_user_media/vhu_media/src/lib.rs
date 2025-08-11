use std::collections::HashMap;
use std::convert;
use std::io;
use std::os::fd::BorrowedFd;
use std::os::unix::io::AsRawFd;

use libc::_SC_PAGESIZE;
use libc::sysconf;
use log::error;
use log::info;
use thiserror::Error as ThisError;
use vhost::vhost_user::Backend;
use vhost::vhost_user::VhostUserFrontendReqHandler;
use vhost::vhost_user::message::VhostUserMMap;
use vhost::vhost_user::message::VhostUserMMapFlags;
use vhost::vhost_user::message::VhostUserProtocolFeatures;
use vhost::vhost_user::message::VhostUserVirtioFeatures;
use vhost_user_backend::{VhostUserBackendMut, VringRwLock, VringT};
use virtio_bindings::bindings::virtio_config::VIRTIO_F_NOTIFY_ON_EMPTY;
use virtio_bindings::bindings::virtio_config::VIRTIO_F_VERSION_1;
use virtio_bindings::bindings::virtio_ring::VIRTIO_RING_F_EVENT_IDX;
use virtio_media::VirtioMediaDevice;
use virtio_media::VirtioMediaDeviceSession;
use virtio_media::VirtioMediaEventQueue;
use virtio_media::VirtioMediaHostMemoryMapper;
use virtio_media::io::ReadFromDescriptorChain;
use virtio_media::io::WriteToDescriptorChain;
use virtio_media::protocol::CloseCmd;
use virtio_media::protocol::CmdHeader;
use virtio_media::protocol::IoctlCmd;
use virtio_media::protocol::MmapCmd;
use virtio_media::protocol::MmapResp;
use virtio_media::protocol::MunmapCmd;
use virtio_media::protocol::MunmapResp;
use virtio_media::protocol::OpenResp;
use virtio_media::protocol::V4l2Event;
use virtio_media::protocol::VirtioMediaDeviceConfig;
use virtio_media::protocol::{self, V4l2Ioctl};
use virtio_queue::DescriptorChain;
use virtio_queue::QueueOwnedT;
use virtio_queue::Reader;
use virtio_queue::Writer;
use vm_allocator::AddressAllocator;
use vm_allocator::RangeInclusive;
use vm_memory::GuestAddressSpace;
use vm_memory::GuestMemoryAtomic;
use vm_memory::GuestMemoryLoadGuard;
use vm_memory::GuestMemoryMmap;
use vmm_sys_util::epoll::EventSet;
use vmm_sys_util::event::{EventConsumer, EventNotifier};
use vmm_sys_util::event::{EventFlag, new_event_consumer_and_notifier};

#[derive(Debug, ThisError)]
/// Errors related to vhost-user-media daemon.
pub(crate) enum VhuMediaBackendError {
    #[error("Failed to handle event, didn't match EPOLLIN")]
    HandleEventNotEpollIn,
    #[error("Failed to handle unknown event")]
    HandleEventUnknown,
    #[error("Descriptor chain error")]
    DescriptorChainError(virtio_queue::Error),
    #[error("I/O error")]
    Io(#[from] std::io::Error),
    #[error("Virtio media device not created")]
    VirtioMediaDeviceNotCreated,
    #[error("Descriptor is unavailable")]
    DescriptorUnavailable,
}

impl convert::From<VhuMediaBackendError> for io::Error {
    fn from(e: VhuMediaBackendError) -> Self {
        io::Error::other(e)
    }
}

type VhuMediaDescriptorChain = DescriptorChain<GuestMemoryLoadGuard<GuestMemoryMmap<()>>>;

pub struct HostMemoryMapper {
    backend: Backend,
    allocator: AddressAllocator,
    allocated_ranges_map: HashMap<u64, u64>,
}

impl HostMemoryMapper {
    fn new(backend: Backend, allocator: AddressAllocator) -> HostMemoryMapper {
        HostMemoryMapper {
            backend: backend,
            allocator: allocator,
            allocated_ranges_map: HashMap::new(),
        }
    }
}

impl VirtioMediaHostMemoryMapper for HostMemoryMapper {
    fn add_mapping(
        &mut self,
        buffer: BorrowedFd,
        length: u64,
        _offset: u64,
        rw: bool,
    ) -> Result<u64, i32> {
        let range = self
            .allocator
            .allocate(
                length,
                pagesize() as u64,
                vm_allocator::AllocPolicy::FirstMatch,
            )
            .map_err(|e| {
                error!("allocate error: {}", e);
                libc::ENOMEM
            })?;
        self.allocated_ranges_map.insert(range.start(), range.end());
        let mut flags = VhostUserMMapFlags::MAP_R;
        if rw {
            flags |= VhostUserMMapFlags::MAP_W;
        }
        let msg = VhostUserMMap {
            shmid: 0,
            padding: [0, 0, 0, 0, 0, 0, 0],
            fd_offset: 0,
            shm_offset: range.start(),
            len: length,
            flags: flags.bits(),
        };

        match self.backend.shmem_map(&msg, &buffer.as_raw_fd()) {
            Ok(_) => Ok(range.start()),
            Err(e) => {
                error!("memory map file request error: {}", e);
                Err(libc::EINVAL)
            }
        }
    }

    fn remove_mapping(&mut self, shm_offset: u64) -> Result<(), i32> {
        let end = match self.allocated_ranges_map.remove(&shm_offset) {
            Some(v) => v,
            None => {
                error!("alloc not found, start: {}", shm_offset);
                return Err(libc::EINVAL);
            }
        };
        let range = RangeInclusive::new(shm_offset, end).map_err(|e| {
            error!("invalid range: {}", e);
            libc::EINVAL
        })?;
        self.allocator.free(&range).map_err(|e| {
            error!("free error: {}", e);
            libc::EINVAL
        })?;
        let msg = VhostUserMMap {
            shmid: 0,
            padding: [0, 0, 0, 0, 0, 0, 0],
            fd_offset: 0,
            shm_offset: shm_offset,
            len: 1,
            flags: 0,
        };

        match self.backend.shmem_unmap(&msg) {
            Ok(_) => Ok(()),
            Err(e) => {
                error!("memory unmap file request error: {}", e);
                Err(libc::EINVAL)
            }
        }
    }
}

pub struct EventQueue {
    mem: GuestMemoryLoadGuard<GuestMemoryMmap>,
    vring: VringRwLock,
}

impl EventQueue {
    fn send_events(&mut self, event: V4l2Event) -> Result<(), VhuMediaBackendError> {
        let vring = self.vring.clone();
        let mem = self.mem.clone();
        let requests: Vec<_> = vring
            .get_mut()
            .get_queue_mut()
            .iter(mem)
            .map_err(|e| VhuMediaBackendError::DescriptorChainError(e))?
            .collect();
        if requests.is_empty() {
            return Err(VhuMediaBackendError::DescriptorUnavailable);
        }
        for desc_chain in requests.clone() {
            let mem = self.mem.clone();
            let head_index = desc_chain.head_index();
            let mut writer = desc_chain
                .writer(&mem)
                .map_err(|e| VhuMediaBackendError::DescriptorChainError(e))?;
            if let Err(e) = match event {
                V4l2Event::DequeueBuffer(e) => WriteToDescriptorChain::write_obj(&mut writer, e),
                V4l2Event::Error(e) => WriteToDescriptorChain::write_obj(&mut writer, e),
                V4l2Event::Event(e) => WriteToDescriptorChain::write_obj(&mut writer, e),
            } {
                return Err(VhuMediaBackendError::Io(e));
            }
            vring
                .get_mut()
                .add_used(head_index, writer.bytes_written() as u32)
                .map_err(|e| VhuMediaBackendError::DescriptorChainError(e))?;
            vring
                .signal_used_queue()
                .map_err(|e| VhuMediaBackendError::Io(e))?;
            break;
        }
        Ok(())
    }
}

impl VirtioMediaEventQueue for EventQueue {
    fn send_event(&mut self, event: V4l2Event) {
        if let Err(e) = self.send_events(event) {
            error!("send event failed with error: {}", e);
        }
    }
}

pub struct VhuMediaBackend<
    S: VirtioMediaDeviceSession,
    D: for<'a> VirtioMediaDevice<Reader<'a>, Writer<'a>>,
    F: Fn(EventQueue, HostMemoryMapper) -> D,
> {
    backend: Option<Backend>,
    config: VirtioMediaDeviceConfig,
    event_idx: bool,
    exit_event_fds: Vec<(EventConsumer, EventNotifier)>,
    mem: Option<GuestMemoryLoadGuard<GuestMemoryMmap>>,
    sessions: HashMap<u32, S>,
    session_id_counter: u32,
    device: Option<D>,
    create_device_fn: F,
}

impl<S, D, F> VhuMediaBackend<S, D, F>
where
    S: VirtioMediaDeviceSession + Send + Sync,
    D: for<'a> VirtioMediaDevice<Reader<'a>, Writer<'a>, Session = S> + Send + Sync,
    F: Fn(EventQueue, HostMemoryMapper) -> D + Send + Sync,
{
    pub fn new(config: VirtioMediaDeviceConfig, create_device_fn: F) -> Self {
        let mut backend = VhuMediaBackend {
            backend: None,
            config,
            event_idx: false,
            exit_event_fds: vec![],
            mem: None,
            sessions: Default::default(),
            session_id_counter: 0,
            device: None,
            create_device_fn: create_device_fn,
        };
        // Create a event_fd for each thread. We make it NONBLOCKing in
        // order to allow tests maximum flexibility in checking whether
        // signals arrived or not.
        backend.exit_event_fds = (0..backend.queues_per_thread().len())
            .map(|_| {
                new_event_consumer_and_notifier(EventFlag::NONBLOCK)
                    .expect("Failed to new EventNotifier and EventConsumer")
            })
            .collect();

        backend
    }

    fn process_commandq_requests(
        &mut self,
        requests: Vec<VhuMediaDescriptorChain>,
        vring: &VringRwLock,
    ) -> Result<(), VhuMediaBackendError> {
        if requests.is_empty() {
            info!("no pending requests");
            return Ok(());
        }
        let device = self
            .device
            .as_mut()
            .ok_or(VhuMediaBackendError::VirtioMediaDeviceNotCreated)?;
        let mem = self.mem.as_ref().unwrap().clone();
        for desc_chain in requests.clone() {
            let head_index = desc_chain.head_index();
            let mut reader = desc_chain
                .clone()
                .reader(&mem)
                .map_err(|e| VhuMediaBackendError::DescriptorChainError(e))?;
            let mut writer = desc_chain
                .writer(&mem)
                .map_err(|e| VhuMediaBackendError::DescriptorChainError(e))?;
            let hdr = ReadFromDescriptorChain::read_obj::<CmdHeader>(&mut reader)
                .map_err(|e| VhuMediaBackendError::Io(e))?;
            match hdr.cmd {
                protocol::VIRTIO_MEDIA_CMD_OPEN => {
                    let session_id = self.session_id_counter;
                    match device.new_session(session_id) {
                        Ok(session) => {
                            self.sessions.insert(session_id, session);
                            self.session_id_counter += 1;
                            writer
                                .write_response(OpenResp::ok(session_id))
                                .map_err(|e| VhuMediaBackendError::Io(e))?;
                        }
                        Err(e) => {
                            error!("device new session error: {}", e);
                            writer
                                .write_err_response(e)
                                .map_err(|e| VhuMediaBackendError::Io(e))?;
                        }
                    }
                }
                protocol::VIRTIO_MEDIA_CMD_CLOSE => {
                    let cmd = ReadFromDescriptorChain::read_obj::<CloseCmd>(&mut reader)
                        .map_err(|e| VhuMediaBackendError::Io(e))?;
                    match self.sessions.remove(&cmd.session_id) {
                        Some(session) => device.close_session(session),
                        None => {
                            error!("session id not found: {}", cmd.session_id);
                            writer
                                .write_err_response(libc::EINVAL)
                                .map_err(|e| VhuMediaBackendError::Io(e))?;
                        }
                    }
                }
                protocol::VIRTIO_MEDIA_CMD_IOCTL => {
                    let cmd = ReadFromDescriptorChain::read_obj::<IoctlCmd>(&mut reader)
                        .map_err(|e| VhuMediaBackendError::Io(e))?;
                    match self.sessions.get_mut(&cmd.session_id) {
                        Some(session) => match V4l2Ioctl::n(cmd.code) {
                            Some(ioctl) => {
                                device
                                    .do_ioctl(session, ioctl, &mut reader, &mut writer)
                                    .map_err(|e| VhuMediaBackendError::Io(e))?;
                            }
                            None => {
                                error!("unknown ioctl code {}", cmd.code);
                                writer
                                    .write_err_response(libc::ENOTTY)
                                    .map_err(|e| VhuMediaBackendError::Io(e))?;
                            }
                        },
                        None => {
                            error!("session id not found: {}", cmd.session_id);
                            writer
                                .write_err_response(libc::EINVAL)
                                .map_err(|e| VhuMediaBackendError::Io(e))?;
                        }
                    }
                }
                protocol::VIRTIO_MEDIA_CMD_MMAP => {
                    let cmd = ReadFromDescriptorChain::read_obj::<MmapCmd>(&mut reader)
                        .map_err(|e| VhuMediaBackendError::Io(e))?;
                    match self.sessions.get_mut(&cmd.session_id) {
                        Some(session) => match device.do_mmap(session, cmd.flags, cmd.offset) {
                            Ok((guest_addr, size)) => {
                                writer
                                    .write_response(MmapResp::ok(guest_addr, size))
                                    .map_err(|e| VhuMediaBackendError::Io(e))?;
                            }
                            Err(e) => {
                                error!("device mmap error: {}", e);
                                writer
                                    .write_err_response(e)
                                    .map_err(|e| VhuMediaBackendError::Io(e))?;
                            }
                        },
                        None => {
                            error!("session id not found: {}", cmd.session_id);
                            writer
                                .write_err_response(libc::EINVAL)
                                .map_err(|e| VhuMediaBackendError::Io(e))?;
                        }
                    }
                }
                protocol::VIRTIO_MEDIA_CMD_MUNMAP => {
                    let cmd = ReadFromDescriptorChain::read_obj::<MunmapCmd>(&mut reader)
                        .map_err(|e| VhuMediaBackendError::Io(e))?;
                    match device.do_munmap(cmd.driver_addr) {
                        Ok(()) => {
                            writer
                                .write_response(MunmapResp::ok())
                                .map_err(|e| VhuMediaBackendError::Io(e))?;
                        }
                        Err(e) => {
                            error!("device munmap error: {}", e);
                            writer
                                .write_err_response(libc::EINVAL)
                                .map_err(|e| VhuMediaBackendError::Io(e))?;
                        }
                    }
                }
                unknown_cmd => {
                    error!("unknown virtio media command: {}", unknown_cmd);
                    writer
                        .write_err_response(libc::ENOTTY)
                        .map_err(|e| VhuMediaBackendError::Io(e))?;
                }
            }
            vring
                .get_mut()
                .add_used(head_index, writer.bytes_written() as u32)
                .map_err(|e| VhuMediaBackendError::DescriptorChainError(e))?;
        }

        Ok(())
    }

    fn process_commandq_queue(&mut self, vring: &VringRwLock) -> Result<(), VhuMediaBackendError> {
        let requests: Vec<_> = vring
            .get_mut()
            .get_queue_mut()
            .iter(self.mem.as_ref().unwrap().clone())
            .map_err(|e| VhuMediaBackendError::DescriptorChainError(e))?
            .collect();
        return match self.process_commandq_requests(requests, vring) {
            Ok(()) => {
                vring
                    .signal_used_queue()
                    .map_err(|e| VhuMediaBackendError::Io(e))?;
                Ok(())
            }
            Err(e) => Err(e),
        };
    }
}

const NUM_QUEUES: usize = 2;
// Use 32768 to avoid Err(InvalidParam) in vhost-user-backend/src/handler.rs:set_vring_num
const QUEUE_SIZE: usize = 32768;

const COMMANDQ: u16 = 0;

const EVENTQ: u16 = 1;

impl<S, D, F> VhostUserBackendMut for VhuMediaBackend<S, D, F>
where
    S: VirtioMediaDeviceSession + Send + Sync,
    D: for<'a> VirtioMediaDevice<Reader<'a>, Writer<'a>, Session = S> + Sync + Send,
    F: Fn(EventQueue, HostMemoryMapper) -> D + Sync + Send,
{
    type Vring = VringRwLock;
    type Bitmap = ();

    fn num_queues(&self) -> usize {
        NUM_QUEUES
    }

    fn max_queue_size(&self) -> usize {
        QUEUE_SIZE
    }

    fn features(&self) -> u64 {
        (1 << VIRTIO_F_VERSION_1)
            | (1 << VIRTIO_F_NOTIFY_ON_EMPTY)
            | (1 << VIRTIO_RING_F_EVENT_IDX)
            | VhostUserVirtioFeatures::PROTOCOL_FEATURES.bits()
    }

    fn protocol_features(&self) -> VhostUserProtocolFeatures {
        // Custom protocol feature supported by crosvm frontend.
        const SHARED_MEMORY_REGIONS: u64 = 0x0010_0000;
        let shared_memory_regions =
            unsafe { std::mem::transmute::<u64, VhostUserProtocolFeatures>(SHARED_MEMORY_REGIONS) };
        VhostUserProtocolFeatures::MQ
            | VhostUserProtocolFeatures::CONFIG
            | VhostUserProtocolFeatures::BACKEND_REQ
            | shared_memory_regions
    }

    fn set_event_idx(&mut self, enabled: bool) {
        self.event_idx = enabled;
    }

    fn update_memory(&mut self, mem: GuestMemoryAtomic<GuestMemoryMmap>) -> io::Result<()> {
        self.mem = Some(mem.memory());
        Ok(())
    }

    fn set_backend_req_fd(&mut self, backend: Backend) {
        self.backend = Some(backend);
    }

    fn handle_event(
        &mut self,
        device_event: u16,
        evset: EventSet,
        vrings: &[VringRwLock],
        _thread_id: usize,
    ) -> io::Result<()> {
        if evset != EventSet::IN {
            return Err(VhuMediaBackendError::HandleEventNotEpollIn.into());
        }
        match device_event {
            COMMANDQ => {
                let vring = &vrings[COMMANDQ as usize];
                if self.event_idx {
                    // vm-virtio's Queue implementation only checks avail_index
                    // once, so to properly support EVENT_IDX we need to keep
                    // calling process_queue() until it stops finding new
                    // requests on the queue.
                    loop {
                        vring.disable_notification().unwrap();
                        self.process_commandq_queue(vring)?;
                        if !vring.enable_notification().unwrap() {
                            break;
                        }
                    }
                } else {
                    self.process_commandq_queue(vring)?;
                }
            }
            EVENTQ => {
                let vring = &vrings[EVENTQ as usize];
                let event_queue = EventQueue {
                    vring: vring.clone(),
                    mem: self.mem.as_ref().unwrap().clone(),
                };
                const HOST_MAPPER_RANGE: u64 = 1 << 32;
                let allocator =
                    vm_allocator::AddressAllocator::new(0, HOST_MAPPER_RANGE - 1).unwrap();
                let host_mapper =
                    HostMemoryMapper::new(self.backend.as_ref().unwrap().clone(), allocator);
                self.device = Some((self.create_device_fn)(event_queue, host_mapper));
            }
            _ => {
                return Err(VhuMediaBackendError::HandleEventUnknown.into());
            }
        }
        Ok(())
    }

    fn exit_event(&self, thread_index: usize) -> Option<(EventConsumer, EventNotifier)> {
        self.exit_event_fds.get(thread_index).map(|(s, r)| {
            (
                s.try_clone().expect("Failed to clone EventConsumer"),
                r.try_clone().expect("Failed to clone EventNotifier"),
            )
        })
    }

    fn queues_per_thread(&self) -> Vec<u64> {
        return vec![3];
    }

    fn get_config(&self, offset: u32, size: u32) -> Vec<u8> {
        let offset = offset as usize;
        let size = size as usize;
        let buf = self.config.as_ref();
        if offset + size > buf.len() {
            return Vec::new();
        }

        buf[offset..offset + size].to_vec()
    }
}

/// Safe wrapper for `sysconf(_SC_PAGESIZE)`.
#[inline(always)]
fn pagesize() -> usize {
    // SAFETY:
    // Trivially safe
    unsafe { sysconf(_SC_PAGESIZE) as usize }
}
