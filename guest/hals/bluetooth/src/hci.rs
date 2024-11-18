// Copyright 2024, The Android Open Source Project
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

//! Bluetooth HCI Service implementation.

use android_hardware_bluetooth::aidl::android::hardware::bluetooth::{
    IBluetoothHci::IBluetoothHci, IBluetoothHciCallbacks::IBluetoothHciCallbacks, Status::Status,
};

use binder::{DeathRecipient, IBinder, Interface, Strong};
use log::{error, info, trace, warn};
use std::fs;
use std::io::{Read, Write};
use std::os::fd::AsRawFd;
use std::os::unix::fs::OpenOptionsExt;
use std::sync::mpsc;
use std::sync::{Arc, Mutex};

#[derive(Clone, Copy, Debug)]
enum Idc {
    Command = 1,
    AclData = 2,
    ScoData = 3,
    Event = 4,
    IsoData = 5,
}

impl Idc {
    const ACL_DATA: u8 = Idc::AclData as u8;
    const SCO_DATA: u8 = Idc::ScoData as u8;
    const EVENT: u8 = Idc::Event as u8;
    const ISO_DATA: u8 = Idc::IsoData as u8;
}

enum ClientState {
    Closed,
    Opened {
        initialized: bool,
        callbacks: Strong<dyn IBluetoothHciCallbacks>,
        _death_recipient: DeathRecipient,
    },
}

struct ServiceState {
    writer: fs::File,
    client_state: ClientState,
}

pub struct BluetoothHci {
    _handle: std::thread::JoinHandle<()>,
    service_state: Arc<Mutex<ServiceState>>,
}

/// Configure a file descriptor as raw fd.
fn make_raw(file: fs::File) -> std::io::Result<fs::File> {
    use nix::sys::termios::*;
    let mut attrs = tcgetattr(&file)?;
    cfmakeraw(&mut attrs);
    tcsetattr(&file, SetArg::TCSANOW, &attrs)?;
    Ok(file)
}

/// Clear all data that might be left in the virtio-console
/// device from a previous session.
fn clear(mut file: fs::File) -> std::io::Result<fs::File> {
    use nix::fcntl::*;
    let mut flags = OFlag::from_bits_truncate(fcntl(file.as_raw_fd(), FcntlArg::F_GETFL)?);

    // Make the input file nonblocking when checking if any data
    // is available to read().
    flags.insert(OFlag::O_NONBLOCK);
    fcntl(file.as_raw_fd(), FcntlArg::F_SETFL(flags))?;

    // Drain bytes present in the file.
    let mut data = [0; 4096];
    loop {
        match file.read(&mut data) {
            // The return value 0 indicates that the file was
            // closed remotely.
            Ok(0) => panic!("failed to clear the serial device"),
            Ok(size) if size == data.len() => (),
            Ok(_) => break,
            Err(err) if err.kind() == std::io::ErrorKind::WouldBlock => break,
            Err(err) => return Err(err),
        }
    }

    // Restore the input file to blocking.
    flags.remove(OFlag::O_NONBLOCK);
    fcntl(file.as_raw_fd(), FcntlArg::F_SETFL(flags))?;

    Ok(file)
}

impl BluetoothHci {
    pub fn new(path: &str) -> Self {
        // Open the serial file and configure it as raw file
        // descriptor.
        let mut reader = fs::OpenOptions::new()
            .read(true)
            .write(true)
            .create(false)
            .open(path)
            .and_then(make_raw)
            .and_then(clear)
            .expect("failed to open the serial device");
        let writer = reader.try_clone().expect("failed to clone serial for writing");

        // Create the chip
        let service_state =
            Arc::new(Mutex::new(ServiceState { writer, client_state: ClientState::Closed }));

        // Spawn the thread that will run the polling loop.
        let handle = {
            let service_state = service_state.clone();
            std::thread::spawn(move || loop {
                let mut data = [0; 4096];

                // Read the packet idc.
                reader.read_exact(&mut data[0..1]).unwrap();
                let idc = data[0];

                // Determine the header size.
                let header_size = 1 + match idc {
                    Idc::ACL_DATA => 4,
                    Idc::SCO_DATA => 3,
                    Idc::ISO_DATA => 4,
                    Idc::EVENT => 2,
                    _ => panic!("received invalid IDC bytes 0x{:02x}", idc),
                };

                // Read the packet header bytes.
                reader.read_exact(&mut data[1..header_size]).unwrap();

                // Determine the payload size.
                let packet_size = header_size
                    + match idc {
                        Idc::ACL_DATA => u16::from_le_bytes([data[3], data[4]]) as usize,
                        Idc::SCO_DATA => data[3] as usize,
                        Idc::ISO_DATA => (u16::from_le_bytes([data[3], data[4]]) & 0x3fff) as usize,
                        Idc::EVENT => data[2] as usize,
                        _ => unreachable!(),
                    };

                // Read the packet payload bytes.
                reader.read_exact(&mut data[header_size..packet_size]).unwrap();

                trace!("read packet: {:?}", &data[..packet_size]);

                // Forward the packet to the host stack.
                {
                    let mut service_state = service_state.lock().unwrap();
                    match service_state.client_state {
                        ClientState::Opened { ref callbacks, ref mut initialized, .. }
                            if !*initialized =>
                        {
                            // While in initialization is pending, all packets are ignored except for the
                            // HCI Reset Complete event.
                            if matches!(
                                &data[0..packet_size],
                                [Idc::EVENT, 0x0e, 0x04, 0x01, 0x03, 0x0c, 0x00]
                            ) {
                                // The initialization of the controller is now complete,
                                // report the status to the Host stack.
                                callbacks.initializationComplete(Status::SUCCESS).unwrap();
                                *initialized = true;
                            }
                        }
                        ClientState::Opened { ref callbacks, .. } => match idc {
                            Idc::ACL_DATA => callbacks.aclDataReceived(&data[1..packet_size]),
                            Idc::SCO_DATA => callbacks.scoDataReceived(&data[1..packet_size]),
                            Idc::ISO_DATA => callbacks.isoDataReceived(&data[1..packet_size]),
                            Idc::EVENT => callbacks.hciEventReceived(&data[1..packet_size]),
                            _ => unreachable!(),
                        }
                        .expect("failed to send HCI packet to host"),
                        ClientState::Closed => (),
                    }
                }
            })
        };

        BluetoothHci { _handle: handle, service_state }
    }

    fn send(&self, idc: Idc, data: &[u8]) -> binder::Result<()> {
        let mut service_state = self.service_state.lock().unwrap();

        if !matches!(service_state.client_state, ClientState::Opened { .. }) {
            error!("IBluetoothHci::sendXX: not initialized");
            return Err(binder::ExceptionCode::ILLEGAL_STATE.into());
        }

        service_state.writer.write_all(&[idc as u8]).unwrap();
        service_state.writer.write_all(data).unwrap();

        Ok(())
    }
}

impl Interface for BluetoothHci {}

impl IBluetoothHci for BluetoothHci {
    fn initialize(&self, callbacks: &Strong<dyn IBluetoothHciCallbacks>) -> binder::Result<()> {
        info!("IBluetoothHci::initialize");

        let mut service_state = self.service_state.lock().unwrap();

        if matches!(service_state.client_state, ClientState::Opened { .. }) {
            error!("IBluetoothHci::initialize: already initialized");
            callbacks.initializationComplete(Status::ALREADY_INITIALIZED)?;
            return Ok(());
        }

        let mut death_recipient = {
            let service_state = self.service_state.clone();
            DeathRecipient::new(move || {
                warn!("IBluetoothHci service has died");
                let mut service_state = service_state.lock().unwrap();
                service_state.client_state = ClientState::Closed;
            })
        };

        callbacks.as_binder().link_to_death(&mut death_recipient)?;

        service_state.client_state = ClientState::Opened {
            initialized: false,
            callbacks: callbacks.clone(),
            _death_recipient: death_recipient,
        };

        // In order to emulate hardware reset of the controller,
        // the HCI Reset command is sent from the HAL directly to clear
        // all controller state.
        // IBluetoothHciCallback.initializationComplete will be invoked
        // the HCI Reset complete event is received.
        service_state.writer.write_all(&[0x01, 0x03, 0x0c, 0x00]).unwrap();

        Ok(())
    }

    fn close(&self) -> binder::Result<()> {
        info!("IBluetoothHci::close");

        let mut service_state = self.service_state.lock().unwrap();
        service_state.client_state = ClientState::Closed;

        Ok(())
    }

    fn sendAclData(&self, data: &[u8]) -> binder::Result<()> {
        info!("IBluetoothHci::sendAclData");

        self.send(Idc::AclData, data)
    }

    fn sendHciCommand(&self, data: &[u8]) -> binder::Result<()> {
        info!("IBluetoothHci::sendHciCommand");

        self.send(Idc::Command, data)
    }

    fn sendIsoData(&self, data: &[u8]) -> binder::Result<()> {
        info!("IBluetoothHci::sendIsoData");

        self.send(Idc::IsoData, data)
    }

    fn sendScoData(&self, data: &[u8]) -> binder::Result<()> {
        info!("IBluetoothHci::sendScoData");

        self.send(Idc::ScoData, data)
    }
}
