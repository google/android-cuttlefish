// Copyright 2023, The Android Open Source Project
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

//! NFC Service implementation with NFC AIDL HAL (`INfc.aidl`)
//!
//! `INfc.aidl` only has blocking calls, but calls are called by multiple thread
//! so implementation should prepare calls from multiple thread.

use crate::nci;

use android_hardware_nfc::aidl::android::hardware::nfc::{
    INfc::INfcAsyncServer, INfcClientCallback::INfcClientCallback, NfcCloseType::NfcCloseType,
    NfcConfig::NfcConfig, NfcEvent::NfcEvent, NfcStatus::NfcStatus,
};
use async_trait::async_trait;
use binder::{DeathRecipient, IBinder, Interface, Strong};
use log::{debug, error, info};
use nix::sys::termios;
use std::path::Path;
use std::sync::Arc;
use tokio::fs::{File, OpenOptions};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::sync::Mutex;
use tokio::task::JoinSet;
const BUF_SIZE: usize = 1024;
const NCI_HEADER_SIZE: usize = 3;
const DBG: bool = true;

struct NfcClient {
    callback: Strong<dyn INfcClientCallback>,
    death_recipient: DeathRecipient,
}

impl NfcClient {
    fn send_data_callback(&self, data: &[u8]) -> binder::Result<()> {
        match self.callback.sendData(data) {
            Err(err) => {
                info!("Failed to send data: {err}");
                Err(err)
            }
            _ => Ok(()),
        }
    }

    fn send_event_callback(&self, event: NfcEvent, status: NfcStatus) -> binder::Result<()> {
        match self.callback.sendEvent(event, status) {
            Err(err) => {
                info!("Failed to send event: {err}");
                Err(err)
            }
            _ => Ok(()),
        }
    }
}

#[derive(Default)]
enum NfcSession {
    #[default]
    Closed,
    Opened(NfcClient),
}

#[derive(Default)]
struct NfcHalConfig {
    dbg_logging: bool,
}

struct NfcServiceStatus {
    writer: File,
    session: NfcSession,
}

impl NfcServiceStatus {
    fn unwrap_mut_opened(&mut self) -> &mut NfcClient {
        match self.session {
            NfcSession::Opened(ref mut client) => client,
            _ => unreachable!(),
        }
    }

    fn ensure_opened(&self) -> binder::Result<&NfcClient> {
        match self.session {
            NfcSession::Opened(ref client) => Ok(client),
            _ => {
                error!("NFC isn't opened");
                Err(binder::Status::new_service_specific_error(NfcStatus::FAILED.0, None))
            }
        }
    }

    fn ensure_opened_mut(&mut self) -> binder::Result<(&mut File, &mut NfcClient)> {
        match self.session {
            NfcSession::Opened(ref mut client) => Ok((&mut self.writer, client)),
            _ => {
                error!("NFC isn't opened");
                Err(binder::Status::new_service_specific_error(NfcStatus::FAILED.0, None))
            }
        }
    }

    async fn close(&mut self) -> binder::Result<()> {
        match self.session {
            NfcSession::Opened(ref mut client) => {
                client.callback.as_binder().unlink_to_death(&mut client.death_recipient)?;
                client.send_event_callback(NfcEvent::CLOSE_CPLT, NfcStatus::OK)?;
                self.session = NfcSession::Closed;
                Ok(())
            }
            _ => Err(binder::Status::new_service_specific_error(NfcStatus::FAILED.0, None)),
        }
    }
}

pub struct NfcService {
    _tasks: JoinSet<()>,
    status: Arc<Mutex<NfcServiceStatus>>,
    config: Arc<Mutex<NfcHalConfig>>,
}

fn set_console_fd_raw(file: &File) {
    let mut attrs = termios::tcgetattr(file).expect("Failed to setup virtio-console to raw mode");
    termios::cfmakeraw(&mut attrs);
    termios::tcsetattr(file, termios::SetArg::TCSANOW, &attrs)
        .expect("Failed to set virtio-console to raw mode");

    let raw_attrs = termios::tcgetattr(file).expect("Failed to validate virtio-console mode");
    if attrs != raw_attrs {
        panic!("Failed to set virtio-console to raw mode. Only partially applied");
    }
}

impl NfcService {
    pub async fn new(dev_path: &Path) -> NfcService {
        // Important notes:
        // - Must clone FD for writing and reading. Otherwise can't read data from host side.
        // - Must not be closed while HAL is running. Otherwise packet loss may happen.
        let writer = OpenOptions::new()
            .read(true)
            .write(true)
            .open(dev_path)
            .await
            .expect("Failed to open virtio-console device");

        // Make FD raw mode for sending raw bytes via console driver (virtio-console),
        set_console_fd_raw(&writer);

        // Must clone FD -- otherwise read may not get incoming data from host side.
        let mut reader =
            writer.try_clone().await.expect("Failed to prepare virtio-console device for reading");
        let mut tasks = JoinSet::new();
        let status = Arc::new(Mutex::new(NfcServiceStatus { writer, session: Default::default() }));
        let status_clone = status.clone();

        // Keep this task running forever to prevent packet loss. read_exact() may partially read
        // packets and dropped if cancelled.
        tasks.spawn(async move {
            let mut buf = [0_u8; BUF_SIZE];
            loop {
                reader
                    .read_exact(&mut buf[0..NCI_HEADER_SIZE])
                    .await
                    .expect("Failed to read from virtio-console device");
                let total_packet_length = (buf[2] as usize) + NCI_HEADER_SIZE;
                reader
                    .read_exact(&mut buf[NCI_HEADER_SIZE..total_packet_length])
                    .await
                    .expect("Failed to read from virtio-console device");

                info!("read sz={}", total_packet_length);
                if let Err(e) = log_packet(&buf[0..total_packet_length]) {
                    debug!(
                        "+ Unidentified packet ({e:?}): bytes={:?}",
                        &buf[0..total_packet_length]
                    );
                }

                let status = status_clone.lock().await;
                if let NfcSession::Opened(ref client) = status.session {
                    if let Err(e) = client.send_data_callback(&buf[0..total_packet_length]) {
                        info!("Failed to send data callback. Maybe closed?: err={e:?}");
                        // If client is disconnected, DeathRecipient will handle clean up.
                    }
                } else {
                    info!("Nfc service is closed. Dropping incoming packets");
                }
            }
        });

        NfcService { _tasks: tasks, status, config: Default::default() }
    }
}

fn log_packet(packet: &[u8]) -> Result<(), anyhow::Error> {
    if !DBG {
        return Ok(());
    }
    let header = nci::PacketHeader::parse(&packet[0..3])?;
    match header.get_mt() {
        nci::MessageType::Data => {
            let packet = nci::DataPacket::parse(packet)?;
            debug!("+ Packet: {packet:?}");
        }
        _ => {
            let packet = nci::ControlPacket::parse(packet)?;
            debug!("+ Packet: {packet:?}");
        }
    }
    Ok(())
}

impl Interface for NfcService {}

#[async_trait]
impl INfcAsyncServer for NfcService {
    async fn open(&self, callback: &Strong<dyn INfcClientCallback>) -> binder::Result<()> {
        info!("open");

        let mut status = self.status.lock().await;
        if let NfcSession::Opened(ref _client) = status.session {
            info!("already opened. closing first.");
            status.close().await?;
        }

        let status_death_recipient = self.status.clone();
        let mut death_recipient = DeathRecipient::new(move || {
            let mut status = status_death_recipient.blocking_lock();
            if let NfcSession::Opened(ref _client) = status.session {
                info!("Nfc service has died");
                // Just set status to closed, because no need to unlink DeathRecipient nor send event callback.
                status.session = NfcSession::Closed;
            }
        });
        callback.as_binder().link_to_death(&mut death_recipient)?;

        status.session =
            NfcSession::Opened(NfcClient { callback: callback.clone(), death_recipient });

        let session = status.unwrap_mut_opened();
        session.send_event_callback(NfcEvent::OPEN_CPLT, NfcStatus::OK)
    }

    async fn close(&self, _close_type: NfcCloseType) -> binder::Result<()> {
        info!("close");

        let mut status = self.status.lock().await;
        match status.session {
            NfcSession::Opened(_) => status.close().await,
            _ => {
                error!("NFC isn't opened");
                Err(binder::Status::new_service_specific_error(NfcStatus::FAILED.0, None))
            }
        }
    }

    async fn coreInitialized(&self) -> binder::Result<()> {
        info!("coreInitialized");
        let status = self.status.lock().await;
        let client = status.ensure_opened()?;
        client.send_event_callback(NfcEvent::POST_INIT_CPLT, NfcStatus::OK)
    }

    async fn factoryReset(&self) -> binder::Result<()> {
        info!("factoryReset");
        let _status = self.status.lock().await;
        // No-op
        Ok(())
    }

    async fn getConfig(&self) -> binder::Result<NfcConfig> {
        info!("getConfig");
        let _status = self.status.lock().await;
        // TODO: read config from libnfc-hal-cf.conf (/apex/<name>/etc)
        Ok(NfcConfig {
            nfaPollBailOutMode: true,
            maxIsoDepTransceiveLength: 0xFEFF,
            defaultOffHostRoute: 0x81_u8 as i8,
            defaultOffHostRouteFelica: 0x81_u8 as i8,
            defaultSystemCodeRoute: 0x00,
            defaultSystemCodePowerState: 0x3B,
            defaultRoute: 0x00,
            offHostRouteUicc: vec![0x81],
            offHostRouteEse: vec![0x81],
            defaultIsoDepRoute: 0x81_u8 as i8,
            ..Default::default()
        })
    }

    async fn powerCycle(&self) -> binder::Result<()> {
        info!("powerCycle");
        let status = self.status.lock().await;
        let client = status.ensure_opened()?;
        client.send_event_callback(NfcEvent::OPEN_CPLT, NfcStatus::OK)
    }

    async fn preDiscover(&self) -> binder::Result<()> {
        info!("preDiscover");
        let status = self.status.lock().await;
        let client = status.ensure_opened()?;
        client.send_event_callback(NfcEvent::PRE_DISCOVER_CPLT, NfcStatus::OK)
    }

    async fn write(&self, data: &[u8]) -> binder::Result<i32> {
        info!("write, sz={}", data.len());
        let mut status = self.status.lock().await;
        let (writer, _) = status.ensure_opened_mut()?;

        writer.write_all(data).await.expect("Failed to write virtio-console device");

        if let Err(e) = log_packet(data) {
            debug!("+ Unidentified packet ({e:?}): bytes={data:?}");
        }

        Ok(data.len().try_into().unwrap())
    }

    async fn setEnableVerboseLogging(&self, enable: bool) -> binder::Result<()> {
        info!("setEnableVerboseLogging");
        let mut config = self.config.lock().await;
        config.dbg_logging = enable;
        Ok(())
    }

    async fn isVerboseLoggingEnabled(&self) -> binder::Result<bool> {
        info!("isVerboseLoggingEnabled");
        let config = self.config.lock().await;
        Ok(config.dbg_logging)
    }

    async fn controlGranted(&self) -> binder::Result<NfcStatus> {
        info!("controlGranted");
        Ok(NfcStatus::OK)
    }
}
