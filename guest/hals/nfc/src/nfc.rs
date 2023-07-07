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

use android_hardware_nfc::aidl::android::hardware::nfc::{
    INfc::INfc, INfcClientCallback::INfcClientCallback, NfcCloseType::NfcCloseType,
    NfcConfig::NfcConfig, NfcEvent::NfcEvent, NfcStatus::NfcStatus,
};
use binder::{DeathRecipient, IBinder, Interface, Strong};
use log::{error, info};
use std::sync::{Arc, Mutex};

struct NfcClient {
    callback: Strong<dyn INfcClientCallback>,
    death_recipient: DeathRecipient,
}

impl NfcClient {
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
enum NfcServiceStatus {
    #[default]
    Closed,
    Opened(NfcClient),
}

impl NfcServiceStatus {
    fn ensure_opened(&self) -> binder::Result<&NfcClient> {
        match self {
            NfcServiceStatus::Opened(client) => Ok(client),
            _ => {
                error!("NFC isn't opened");
                Err(binder::Status::new_service_specific_error(NfcStatus::FAILED.0, None))
            }
        }
    }
}

#[derive(Default)]
struct NfcHalConfig {
    dbg_logging: bool,
}

#[derive(Default)]
pub struct NfcService {
    status: Arc<Mutex<NfcServiceStatus>>,
    config: Arc<Mutex<NfcHalConfig>>,
}

impl Interface for NfcService {}

impl INfc for NfcService {
    fn open(&self, callback: &Strong<dyn INfcClientCallback>) -> binder::Result<()> {
        info!("open");
        let mut status = self.status.lock().unwrap();

        if let NfcServiceStatus::Opened(ref mut client) = *status {
            info!("close then open again");
            client.callback.as_binder().unlink_to_death(&mut client.death_recipient)?;
            *status = NfcServiceStatus::Closed;
        }

        let status_clone = self.status.clone();
        let mut death_recipient = DeathRecipient::new(move || {
            let mut status = status_clone.lock().unwrap();
            if let NfcServiceStatus::Opened(ref mut _client) = *status {
                info!("Nfc service has died");
                *status = NfcServiceStatus::Closed;
            }
        });

        callback.as_binder().link_to_death(&mut death_recipient)?;

        *status =
            NfcServiceStatus::Opened(NfcClient { callback: callback.clone(), death_recipient });
        let client = status.ensure_opened()?;
        client.send_event_callback(NfcEvent::OPEN_CPLT, NfcStatus::OK)
    }

    fn close(&self, _close_type: NfcCloseType) -> binder::Result<()> {
        info!("close");

        let mut status = self.status.lock().unwrap();
        match *status {
            NfcServiceStatus::Opened(ref mut client) => {
                client.callback.as_binder().unlink_to_death(&mut client.death_recipient)?;
                client.send_event_callback(NfcEvent::CLOSE_CPLT, NfcStatus::OK)?;
                *status = NfcServiceStatus::Closed;
                Ok(())
            }
            _ => {
                error!("NFC isn't opened");
                Err(binder::Status::new_service_specific_error(NfcStatus::FAILED.0, None))
            }
        }
    }

    fn coreInitialized(&self) -> binder::Result<()> {
        info!("coreInitialized");
        let status = self.status.lock().unwrap();
        let client = status.ensure_opened()?;
        client.send_event_callback(NfcEvent::POST_INIT_CPLT, NfcStatus::OK)
    }

    fn factoryReset(&self) -> binder::Result<()> {
        info!("factoryReset");
        let _status = self.status.lock().unwrap();
        // No-op
        Ok(())
    }

    fn getConfig(&self) -> binder::Result<NfcConfig> {
        info!("getConfig");
        let _status = self.status.lock().unwrap();
        // TODO: read config from /vendor/etc/libnfc-hal-cf.conf
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

    fn powerCycle(&self) -> binder::Result<()> {
        info!("powerCycle");
        let status = self.status.lock().unwrap();
        let client = status.ensure_opened()?;
        client.send_event_callback(NfcEvent::OPEN_CPLT, NfcStatus::OK)
    }

    fn preDiscover(&self) -> binder::Result<()> {
        info!("preDiscover");
        let status = self.status.lock().unwrap();
        let client = status.ensure_opened()?;
        client.send_event_callback(NfcEvent::PRE_DISCOVER_CPLT, NfcStatus::OK)
    }

    fn write(&self, _data: &[u8]) -> binder::Result<i32> {
        info!("write");
        let status = self.status.lock().unwrap();
        let _client = status.ensure_opened();
        // TODO: write NCI state machine
        Ok(0)
    }

    fn setEnableVerboseLogging(&self, enable: bool) -> binder::Result<()> {
        info!("setEnableVerboseLogging");
        let mut config = self.config.lock().unwrap();
        config.dbg_logging = enable;
        Ok(())
    }

    fn isVerboseLoggingEnabled(&self) -> binder::Result<bool> {
        info!("isVerboseLoggingEnabled");
        let config = self.config.lock().unwrap();
        Ok(config.dbg_logging)
    }
}
