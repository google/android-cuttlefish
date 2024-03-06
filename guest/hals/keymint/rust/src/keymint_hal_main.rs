// Copyright 2021, The Android Open Source Project
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

//! This crate implements the KeyMint HAL service in Rust, communicating with a Rust
//! trusted application (TA) running on the Cuttlefish host.

use kmr_hal_nonsecure::{attestation_id_info, get_boot_info};
use log::{debug, error, info};
use std::ops::DerefMut;
use std::os::unix::io::FromRawFd;
use std::panic;
use std::sync::{Arc, Mutex};

/// Device file used to communicate with the KeyMint TA.
static DEVICE_FILE_NAME: &str = "/dev/hvc11";

/// Name of KeyMint binder device instance.
static SERVICE_INSTANCE: &str = "default";

static KM_SERVICE_NAME: &str = "android.hardware.security.keymint.IKeyMintDevice";
static RPC_SERVICE_NAME: &str = "android.hardware.security.keymint.IRemotelyProvisionedComponent";
static CLOCK_SERVICE_NAME: &str = "android.hardware.security.secureclock.ISecureClock";
static SECRET_SERVICE_NAME: &str = "android.hardware.security.sharedsecret.ISharedSecret";

/// Local error type for failures in the HAL service.
#[derive(Debug, Clone)]
struct HalServiceError(String);

/// Read-write file used for communication with host TA.
#[derive(Debug)]
struct FileChannel(std::fs::File);

impl kmr_hal::SerializedChannel for FileChannel {
    const MAX_SIZE: usize = kmr_wire::DEFAULT_MAX_SIZE;

    fn execute(&mut self, serialized_req: &[u8]) -> binder::Result<Vec<u8>> {
        kmr_hal::write_msg(&mut self.0, serialized_req)?;
        kmr_hal::read_msg(&mut self.0)
    }
}

/// Set 'raw' mode for the given file descriptor.
fn set_terminal_raw(fd: libc::c_int) -> Result<(), HalServiceError> {
    // SAFETY: All fields of termios are valid for zero bytes.
    let mut settings: libc::termios = unsafe { std::mem::zeroed() };
    // SAFETY: The pointer is valid because it comes from a reference, and tcgetattr doesn't store
    // it.
    let result = unsafe { libc::tcgetattr(fd, &mut settings) };
    if result < 0 {
        return Err(HalServiceError(format!(
            "Failed to get terminal attributes for {}: {:?}",
            fd,
            std::io::Error::last_os_error()
        )));
    }

    // SAFETY: The pointers are valid because they come from references, and they are not stored
    // beyond the function calls.
    let result = unsafe {
        libc::cfmakeraw(&mut settings);
        libc::tcsetattr(fd, libc::TCSANOW, &settings)
    };
    if result < 0 {
        return Err(HalServiceError(format!(
            "Failed to set terminal attributes for {}: {:?}",
            fd,
            std::io::Error::last_os_error()
        )));
    }
    Ok(())
}

fn main() {
    if let Err(e) = inner_main() {
        panic!("HAL service failed: {:?}", e);
    }
}

fn inner_main() -> Result<(), HalServiceError> {
    // Initialize android logging.
    android_logger::init_once(
        android_logger::Config::default()
            .with_tag("keymint-hal")
            .with_max_level(log::LevelFilter::Info)
            .with_log_buffer(android_logger::LogId::System),
    );
    // Redirect panic messages to logcat.
    panic::set_hook(Box::new(|panic_info| {
        error!("{}", panic_info);
    }));

    info!("KeyMint HAL service is starting.");

    info!("Starting thread pool now.");
    binder::ProcessState::start_thread_pool();

    // Create a connection to the TA.
    let path = std::ffi::CString::new(DEVICE_FILE_NAME).unwrap();
    // SAFETY: The path is a valid C string.
    let fd = unsafe { libc::open(path.as_ptr(), libc::O_RDWR) };
    if fd < 0 {
        return Err(HalServiceError(format!(
            "Failed to open device file '{}': {:?}",
            DEVICE_FILE_NAME,
            std::io::Error::last_os_error()
        )));
    }
    set_terminal_raw(fd)?;
    // SAFETY: The file descriptor is valid because `open` either returns a valid FD or -1, and we
    // checked that it is not negative.
    let channel = Arc::new(Mutex::new(FileChannel(unsafe { std::fs::File::from_raw_fd(fd) })));

    let km_service = kmr_hal::keymint::Device::new_as_binder(channel.clone());
    let service_name = format!("{}/{}", KM_SERVICE_NAME, SERVICE_INSTANCE);
    binder::add_service(&service_name, km_service.as_binder()).map_err(|e| {
        HalServiceError(format!("Failed to register service {} because of {:?}.", service_name, e))
    })?;

    let rpc_service = kmr_hal::rpc::Device::new_as_binder(channel.clone());
    let service_name = format!("{}/{}", RPC_SERVICE_NAME, SERVICE_INSTANCE);
    binder::add_service(&service_name, rpc_service.as_binder()).map_err(|e| {
        HalServiceError(format!("Failed to register service {} because of {:?}.", service_name, e))
    })?;

    let clock_service = kmr_hal::secureclock::Device::new_as_binder(channel.clone());
    let service_name = format!("{}/{}", CLOCK_SERVICE_NAME, SERVICE_INSTANCE);
    binder::add_service(&service_name, clock_service.as_binder()).map_err(|e| {
        HalServiceError(format!("Failed to register service {} because of {:?}.", service_name, e))
    })?;

    let secret_service = kmr_hal::sharedsecret::Device::new_as_binder(channel.clone());
    let service_name = format!("{}/{}", SECRET_SERVICE_NAME, SERVICE_INSTANCE);
    binder::add_service(&service_name, secret_service.as_binder()).map_err(|e| {
        HalServiceError(format!("Failed to register service {} because of {:?}.", service_name, e))
    })?;

    info!("Successfully registered KeyMint HAL services.");

    // Let the TA know information about the boot environment. In a real device this
    // is communicated directly from the bootloader to the TA, but here we retrieve
    // the information from system properties and send from the HAL service.
    // TODO: investigate Cuttlefish bootloader info propagation
    // https://android.googlesource.com/platform/external/u-boot/+/2114f87e56d262220c4dc5e00c3321e99e12204b/boot/android_bootloader_keymint.c
    let boot_req = get_boot_info();
    debug!("boot/HAL->TA: boot info is {:?}", boot_req);
    kmr_hal::send_boot_info(channel.lock().unwrap().deref_mut(), boot_req)
        .map_err(|e| HalServiceError(format!("Failed to send boot info: {:?}", e)))?;

    // Let the TA know information about the userspace environment.
    if let Err(e) = kmr_hal::send_hal_info(channel.lock().unwrap().deref_mut()) {
        error!("Failed to send HAL info: {:?}", e);
    }

    // Let the TA know about attestation IDs. (In a real device these would be pre-provisioned into
    // the TA.)
    let attest_ids = attestation_id_info();
    if let Err(e) = kmr_hal::send_attest_ids(channel.lock().unwrap().deref_mut(), attest_ids) {
        error!("Failed to send attestation ID info: {:?}", e);
    }

    info!("Joining thread pool now.");
    binder::ProcessState::join_thread_pool();
    info!("KeyMint HAL service is terminating.");
    Ok(())
}
