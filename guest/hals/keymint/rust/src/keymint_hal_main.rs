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

use kmr_hal::env::get_property;
use log::{debug, error, info};
use std::ops::DerefMut;
use std::os::unix::io::FromRawFd;
use std::panic;
use std::sync::{Arc, Mutex};

/// Device file used to communicate with the KeyMint TA.
static DEVICE_FILE_NAME: &str = "/dev/hvc3";

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
    let mut settings: libc::termios = unsafe { std::mem::zeroed() };
    let result = unsafe { libc::tcgetattr(fd, &mut settings) };
    if result < 0 {
        return Err(HalServiceError(format!(
            "Failed to get terminal attributes for {}: {:?}",
            fd,
            std::io::Error::last_os_error()
        )));
    }

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
    // TODO(b/239476536): log to the kernel logger, so output generated before Cuttlefish's logcat
    // exporter starts is visible.
    android_logger::init_once(
        android_logger::Config::default()
            .with_tag("keymint-hal")
            .with_min_level(log::Level::Info)
            .with_log_id(android_logger::LogId::System),
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
    let fd = unsafe { libc::open(path.as_ptr(), libc::O_RDWR) };
    if fd < 0 {
        return Err(HalServiceError(format!(
            "Failed to open device file '{}': {:?}",
            DEVICE_FILE_NAME,
            std::io::Error::last_os_error()
        )));
    }
    set_terminal_raw(fd)?;
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

/// Populate attestation ID information based on properties (where available).
fn attestation_id_info() -> kmr_wire::AttestationIdInfo {
    let prop = |name| {
        get_property(name).unwrap_or_else(|_| format!("{} unavailable", name)).as_bytes().to_vec()
    };
    kmr_wire::AttestationIdInfo {
        brand: prop("ro.product.brand"),
        device: prop("ro.product.device"),
        product: prop("ro.product.name"),
        serial: prop("ro.serialno"),
        manufacturer: prop("ro.product.manufacturer"),
        model: prop("ro.product.model"),
        // Currently modem_simulator always returns one fixed value. See `handleGetIMEI` in
        // device/google/cuttlefish/host/commands/modem_simulator/misc_service.cpp for more details.
        // TODO(b/263188546): Use device-specific IMEI values when available.
        imei: b"867400022047199".to_vec(),
        imei2: vec![],
        meid: vec![],
    }
}

/// Get boot information based on system properties.
fn get_boot_info() -> kmr_wire::SetBootInfoRequest {
    // No access to a verified boot key.
    let verified_boot_key = vec![0; 32];
    let vbmeta_digest = get_property("ro.boot.vbmeta.digest").unwrap_or_else(|_| "00".repeat(32));
    let verified_boot_hash = hex::decode(&vbmeta_digest).unwrap_or_else(|_e| {
        error!("failed to parse hex data in '{}'", vbmeta_digest);
        vec![0; 32]
    });
    let device_boot_locked = match get_property("ro.boot.vbmeta.device_state")
        .unwrap_or_else(|_| "no-prop".to_string())
        .as_str()
    {
        "locked" => true,
        "unlocked" => false,
        v => {
            error!("Unknown device_state '{}', treating as unlocked", v);
            false
        }
    };
    let verified_boot_state = match get_property("ro.boot.verifiedbootstate")
        .unwrap_or_else(|_| "no-prop".to_string())
        .as_str()
    {
        "green" => 0,  // Verified
        "yellow" => 1, // SelfSigned
        "orange" => 2, // Unverified,
        "red" => 3,    // Failed,
        v => {
            error!("Unknown boot state '{}', treating as Unverified", v);
            2
        }
    };

    // Attempt to get the boot patchlevel from a system property.  This requires an SELinux
    // permission, so fall back to re-using the OS patchlevel if this can't be done.
    let boot_patchlevel_prop = get_property("ro.vendor.boot_security_patch").unwrap_or_else(|e| {
        error!("Failed to retrieve boot patchlevel: {:?}", e);
        get_property(kmr_hal::env::OS_PATCHLEVEL_PROPERTY)
            .unwrap_or_else(|_| "1970-09-19".to_string())
    });
    let boot_patchlevel =
        kmr_hal::env::extract_patchlevel(&boot_patchlevel_prop).unwrap_or(19700919);

    kmr_wire::SetBootInfoRequest {
        verified_boot_key,
        device_boot_locked,
        verified_boot_state,
        verified_boot_hash,
        boot_patchlevel,
    }
}
