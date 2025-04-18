use std::collections::BTreeMap;

use anyhow::{bail, Context, Result};
use log::debug;
use serde::Deserialize;
use zerocopy::byteorder::little_endian::U32 as Le32;
use zerocopy::{FromBytes, FromZeros, Immutable, IntoBytes};

#[allow(dead_code)]
pub const VIRTIO_INPUT_CFG_UNSET: u8 = 0x00;
pub const VIRTIO_INPUT_CFG_ID_NAME: u8 = 0x01;
pub const VIRTIO_INPUT_CFG_ID_SERIAL: u8 = 0x02;
pub const VIRTIO_INPUT_CFG_ID_DEVIDS: u8 = 0x03;
pub const VIRTIO_INPUT_CFG_PROP_BITS: u8 = 0x10;
pub const VIRTIO_INPUT_CFG_EV_BITS: u8 = 0x11;
pub const VIRTIO_INPUT_CFG_ABS_INFO: u8 = 0x12;

pub const VIRTIO_INPUT_EVENT_SIZE: usize = 8;

/// Calculates the largest multiple of virtio input event size less than or equal to v.
pub fn trim_to_event_size_multiple(v: usize) -> usize {
    v & !(VIRTIO_INPUT_EVENT_SIZE - 1)
}

/// In memory representation of the virtio input configuration area.
#[derive(Copy, Clone, FromBytes, Immutable, IntoBytes)]
#[repr(C)]
struct virtio_input_config {
    pub select: u8,
    pub subsel: u8,
    pub size: u8,
    pub reserved: [u8; 5],
    pub payload: [u8; 128],
}

#[derive(Copy, Clone, FromBytes, Immutable, IntoBytes)]
#[repr(C)]
struct virtio_input_absinfo {
    min: Le32,
    max: Le32,
    fuzz: Le32,
    flat: Le32,
}

impl From<&InputConfigFileAbsInfo> for virtio_input_absinfo {
    fn from(absinfo: &InputConfigFileAbsInfo) -> virtio_input_absinfo {
        virtio_input_absinfo {
            min: Le32::from(absinfo.min),
            max: Le32::from(absinfo.max),
            fuzz: Le32::from(absinfo.fuzz),
            flat: Le32::from(absinfo.flat),
        }
    }
}

/// Bitmap used in the virtio input configuration region.
#[derive(Clone)]
struct VirtioInputBitmap {
    pub bitmap: [u8; 128],
}

impl VirtioInputBitmap {
    pub fn new() -> VirtioInputBitmap {
        VirtioInputBitmap { bitmap: [0u8; 128] }
    }

    /// Length of the minimum array that can hold all set bits in the map.
    pub fn min_size(&self) -> u8 {
        self.bitmap.iter().rposition(|v| *v != 0).map_or(0, |i| i + 1) as u8
    }

    fn set(&mut self, idx: u16) -> Result<()> {
        let byte_pos = (idx / 8) as usize;
        let bit_byte = 1u8 << (idx % 8);
        if byte_pos >= self.bitmap.len() {
            // This would only happen if new event codes (or types, or ABS_*, etc) are defined
            // to be larger than or equal to 1024, in which case a new version
            // of the virtio input protocol needs to be defined.
            bail!("Bitmap index '{}' is out of bitmap bounds ({})", idx, 128);
        }
        self.bitmap[byte_pos] |= bit_byte;
        Ok(())
    }
}

/// Configuration of a virtio input device.
#[derive(Clone)]
pub struct VirtioInputConfig {
    name: String,
    serial_name: String,
    properties: VirtioInputBitmap,
    supported_events: BTreeMap<u16, VirtioInputBitmap>,
    axis_info: BTreeMap<u16, virtio_input_absinfo>,
    // [select, subsel]
    select_bytes: [u8; 2],
}

impl VirtioInputConfig {
    pub fn from_json(device_config_str: &str) -> Result<VirtioInputConfig> {
        let config: InputConfigFile =
            serde_json::from_str(device_config_str).context("Failed to parse JSON string")?;
        debug!("Parsed device config: {:?}", config);

        let mut supported_events = BTreeMap::<u16, VirtioInputBitmap>::new();
        let mut supported_event_types = VirtioInputBitmap::new();
        for event in config.events {
            let mut bitmap = VirtioInputBitmap::new();
            for &event_code in event.supported_events.values() {
                bitmap.set(event_code)?;
            }
            supported_events.insert(event.event_type_code, bitmap);
            debug!("supporting event: {}", event.event_type_code);
            supported_event_types.set(event.event_type_code)?;
        }
        // zero is a special case: return all supported event types (just like EVIOCGBIT)
        supported_events.insert(0, supported_event_types);

        let mut properties = VirtioInputBitmap::new();
        for &property in config.properties.values() {
            properties.set(property)?;
        }

        let axis_info: BTreeMap<u16, virtio_input_absinfo> = config
            .axis_info
            .iter()
            .map(|absinfo| (absinfo.axis_code, virtio_input_absinfo::from(absinfo)))
            .collect();

        Ok(VirtioInputConfig {
            name: config.name,
            serial_name: config.serial_name,
            properties,
            supported_events,
            axis_info,
            select_bytes: [0u8; 2],
        })
    }

    pub fn set_raw(&mut self, offset: u32, buf: &[u8]) -> Result<()> {
        let mut start = offset as usize;
        let mut end = start + buf.len();

        if end > std::mem::size_of::<virtio_input_config>() {
            bail!("Config write out of bounds: start={}, end={}", start, end);
        }

        // The driver doesn't (and shouldn't) write past the first two bytes, but qemu always reads
        // and writes the entire config space regardless of what the driver asks.
        start = std::cmp::min(start, self.select_bytes.len());
        end = std::cmp::min(end, self.select_bytes.len());

        if start == end {
            return Ok(());
        }

        self.select_bytes[start..end].copy_from_slice(&buf[0..end - start]);

        Ok(())
    }

    pub fn get_raw(&self) -> Result<Vec<u8>> {
        let mut config = virtio_input_config::new_zeroed();
        config.select = self.select_bytes[0];
        config.subsel = self.select_bytes[1];
        match config.select {
            VIRTIO_INPUT_CFG_ID_NAME => {
                config.size = self.name.len() as u8;
                config.payload[..self.name.len()].clone_from_slice(self.name.as_bytes());
            }
            VIRTIO_INPUT_CFG_ID_SERIAL => {
                config.size = self.serial_name.len() as u8;
                config.payload[..self.serial_name.len()]
                    .clone_from_slice(self.serial_name.as_bytes());
            }
            VIRTIO_INPUT_CFG_ID_DEVIDS => {
                // {0,0,0,0}
                config.payload = [0u8; 128];
            }
            VIRTIO_INPUT_CFG_PROP_BITS => {
                config.size = self.properties.min_size();
                config.payload = self.properties.bitmap;
            }
            VIRTIO_INPUT_CFG_EV_BITS => {
                if let Some(events) = self.supported_events.get(&u16::from(config.subsel)) {
                    config.size = events.min_size();
                    config.payload = events.bitmap;
                } else {
                    // This is not an error. Some drivers don't request the full list by
                    // setting subsel to 0 and just ask for all types of events instead.
                    config.size = 0;
                }
            }
            VIRTIO_INPUT_CFG_ABS_INFO => {
                let axis_code = config.subsel as u16;
                if let Some(absinfo) = self.axis_info.get(&axis_code) {
                    let size = std::mem::size_of::<virtio_input_absinfo>();
                    config.size = size as u8;
                    config.payload[0..size].copy_from_slice(absinfo.as_bytes());
                } else {
                    config.size = 0;
                }
            }
            _ => {
                bail!("Unsupported config selection: {}", config.select);
            }
        };
        Ok(config.as_bytes().to_vec())
    }
}

#[derive(Debug, Deserialize)]
struct InputConfigFile {
    name: String,
    serial_name: String,
    #[serde(default)]
    properties: BTreeMap<String, u16>,
    events: Vec<InputConfigFileEvent>,
    #[serde(default)]
    axis_info: Vec<InputConfigFileAbsInfo>,
}

#[derive(Debug, Deserialize)]
struct InputConfigFileEvent {
    #[allow(dead_code)]
    event_type: String,
    event_type_code: u16,
    supported_events: BTreeMap<String, u16>,
}

#[derive(Debug, Deserialize)]
struct InputConfigFileAbsInfo {
    #[allow(dead_code)]
    axis: String,
    axis_code: u16,
    min: u32,
    max: u32,
    #[serde(default)]
    fuzz: u32,
    #[serde(default)]
    flat: u32,
}
