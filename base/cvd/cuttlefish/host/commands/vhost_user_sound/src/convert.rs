use anyhow::{bail, Result};
use zerocopy::byteorder::little_endian::{F32 as LeF32, F64 as LeF64, U16 as Le16};

use crate::virtio_snd::*;

pub fn convert_to_s16(input_format: VirtioSndPcmFormat, buffer: &[u8]) -> Result<Vec<i16>> {
    Ok(match input_format {
        VirtioSndPcmFormat::ImaAdpcm
        | VirtioSndPcmFormat::MuLaw
        | VirtioSndPcmFormat::Alaw
        | VirtioSndPcmFormat::DsdU16
        | VirtioSndPcmFormat::DsdU32
        | VirtioSndPcmFormat::Iec958Subframe
        | VirtioSndPcmFormat::DsdU8 => {
            bail!("Unsupported input_format: {}", input_format as u8);
        }
        VirtioSndPcmFormat::S8 => {
            let (samples, []) = buffer.as_chunks::<1>() else {
                bail!(
                    "Buffer doesn't hold a whole number of samples for format: {}",
                    input_format as u8
                )
            };
            samples.into_iter().map(convert_s8_sample_to_s16).collect()
        }
        VirtioSndPcmFormat::U8 => {
            let (samples, []) = buffer.as_chunks::<1>() else {
                bail!(
                    "Buffer doesn't hold a whole number of samples for format: {}",
                    input_format as u8
                )
            };
            samples.into_iter().map(convert_u8_sample_to_s16).collect()
        }
        VirtioSndPcmFormat::S16 => {
            let (samples, []) = buffer.as_chunks::<2>() else {
                bail!(
                    "Buffer doesn't hold a whole number of samples for format: {}",
                    input_format as u8
                )
            };
            samples.into_iter().map(convert_s16_sample_to_s16).collect()
        }
        VirtioSndPcmFormat::U16 => {
            let (samples, []) = buffer.as_chunks::<2>() else {
                bail!(
                    "Buffer doesn't hold a whole number of samples for format: {}",
                    input_format as u8
                )
            };
            samples.into_iter().map(convert_u16_sample_to_s16).collect()
        }
        VirtioSndPcmFormat::S18_3 => {
            let (samples, []) = buffer.as_chunks::<3>() else {
                bail!(
                    "Buffer doesn't hold a whole number of samples for format: {}",
                    input_format as u8
                )
            };
            samples
                .into_iter()
                .map(convert_s18_3_sample_to_s16)
                .collect()
        }
        VirtioSndPcmFormat::U18_3 => {
            let (samples, []) = buffer.as_chunks::<3>() else {
                bail!(
                    "Buffer doesn't hold a whole number of samples for format: {}",
                    input_format as u8
                )
            };
            samples
                .into_iter()
                .map(convert_u18_3_sample_to_s16)
                .collect()
        }
        VirtioSndPcmFormat::S20_3 => {
            let (samples, []) = buffer.as_chunks::<3>() else {
                bail!(
                    "Buffer doesn't hold a whole number of samples for format: {}",
                    input_format as u8
                )
            };
            samples
                .into_iter()
                .map(convert_s20_3_sample_to_s16)
                .collect()
        }
        VirtioSndPcmFormat::U20_3 => {
            let (samples, []) = buffer.as_chunks::<3>() else {
                bail!(
                    "Buffer doesn't hold a whole number of samples for format: {}",
                    input_format as u8
                )
            };
            samples
                .into_iter()
                .map(convert_u20_3_sample_to_s16)
                .collect()
        }
        VirtioSndPcmFormat::S24_3 => {
            let (samples, []) = buffer.as_chunks::<3>() else {
                bail!(
                    "Buffer doesn't hold a whole number of samples for format: {}",
                    input_format as u8
                )
            };
            samples
                .into_iter()
                .map(convert_s24_3_sample_to_s16)
                .collect()
        }
        VirtioSndPcmFormat::U24_3 => {
            let (samples, []) = buffer.as_chunks::<3>() else {
                bail!(
                    "Buffer doesn't hold a whole number of samples for format: {}",
                    input_format as u8
                )
            };
            samples
                .into_iter()
                .map(convert_u24_3_sample_to_s16)
                .collect()
        }
        VirtioSndPcmFormat::S20 => {
            let (samples, []) = buffer.as_chunks::<4>() else {
                bail!(
                    "Buffer doesn't hold a whole number of samples for format: {}",
                    input_format as u8
                )
            };
            samples.into_iter().map(convert_s20_sample_to_s16).collect()
        }
        VirtioSndPcmFormat::U20 => {
            let (samples, []) = buffer.as_chunks::<4>() else {
                bail!(
                    "Buffer doesn't hold a whole number of samples for format: {}",
                    input_format as u8
                )
            };
            samples.into_iter().map(convert_u20_sample_to_s16).collect()
        }
        VirtioSndPcmFormat::S24 => {
            let (samples, []) = buffer.as_chunks::<4>() else {
                bail!(
                    "Buffer doesn't hold a whole number of samples for format: {}",
                    input_format as u8
                )
            };
            samples.into_iter().map(convert_s24_sample_to_s16).collect()
        }
        VirtioSndPcmFormat::U24 => {
            let (samples, []) = buffer.as_chunks::<4>() else {
                bail!(
                    "Buffer doesn't hold a whole number of samples for format: {}",
                    input_format as u8
                )
            };
            samples.into_iter().map(convert_u24_sample_to_s16).collect()
        }
        VirtioSndPcmFormat::S32 => {
            let (samples, []) = buffer.as_chunks::<4>() else {
                bail!(
                    "Buffer doesn't hold a whole number of samples for format: {}",
                    input_format as u8
                )
            };
            samples.into_iter().map(convert_s32_sample_to_s16).collect()
        }
        VirtioSndPcmFormat::U32 => {
            let (samples, []) = buffer.as_chunks::<4>() else {
                bail!(
                    "Buffer doesn't hold a whole number of samples for format: {}",
                    input_format as u8
                )
            };
            samples.into_iter().map(convert_u32_sample_to_s16).collect()
        }
        VirtioSndPcmFormat::Float => {
            let (samples, []) = buffer.as_chunks::<4>() else {
                bail!(
                    "Buffer doesn't hold a whole number of samples for format: {}",
                    input_format as u8
                )
            };
            samples
                .into_iter()
                .map(convert_float_sample_to_s16)
                .collect()
        }
        VirtioSndPcmFormat::Float64 => {
            let (samples, []) = buffer.as_chunks::<8>() else {
                bail!(
                    "Buffer doesn't hold a whole number of samples for format: {}",
                    input_format as u8
                )
            };
            samples
                .into_iter()
                .map(convert_float64_sample_to_s16)
                .collect()
        }
    })
}

// Converts an audio sample in the range 0..2^16-1 to one in the range -2^15..2^15-1.
fn range_shift(value: u16) -> i16 {
    (value ^ 0x8000) as i16
}

// Returns a reference to the most significant M bytes of an N-byte little endian value.
// Panics if M > N.
fn most_significant_bytes_le<const N: usize, const M: usize>(arr: &[u8; N]) -> &[u8; M] {
    arr.last_chunk::<M>().unwrap()
}

fn convert_s8_sample_to_s16(sample: &[u8; 1]) -> i16 {
    i16::from(sample[0] as i8) << 8
}

fn convert_u8_sample_to_s16(sample: &[u8; 1]) -> i16 {
    i16::from((sample[0] ^ 0x80) as i8) << 8
}

fn convert_s16_sample_to_s16(sample: &[u8; 2]) -> i16 {
    Le16::from_bytes(*sample).get() as i16
}

fn convert_u16_sample_to_s16(sample: &[u8; 2]) -> i16 {
    (Le16::from_bytes(*sample).get() ^ 0x8000) as i16
}

fn convert_s18_3_sample_to_s16(sample: &[u8; 3]) -> i16 {
    let i18_0_padded =
        u32::from(sample[0]) | u32::from(sample[1]) << 8 | u32::from(sample[2] & 0b11) << 16;
    (i18_0_padded >> 2) as i16
}

fn convert_u18_3_sample_to_s16(sample: &[u8; 3]) -> i16 {
    let u18_0_padded =
        u32::from(sample[0]) | u32::from(sample[1]) << 8 | u32::from(sample[2] & 0b11) << 16;
    let as_u16 = (u18_0_padded >> 2) as u16;
    range_shift(as_u16)
}

fn convert_s20_3_sample_to_s16(sample: &[u8; 3]) -> i16 {
    let i20_0_padded =
        u32::from(sample[0]) | u32::from(sample[1]) << 8 | u32::from(sample[2] & 0b1111) << 16;
    (i20_0_padded >> 4) as i16
}

fn convert_u20_3_sample_to_s16(sample: &[u8; 3]) -> i16 {
    let u20_0_padded =
        u32::from(sample[0]) | u32::from(sample[1]) << 8 | u32::from(sample[2] & 0b1111) << 16;
    let as_u16 = (u20_0_padded >> 4) as u16;
    range_shift(as_u16)
}

fn convert_s24_3_sample_to_s16(sample: &[u8; 3]) -> i16 {
    Le16::from_bytes(*most_significant_bytes_le(sample)).get() as i16
}

fn convert_u24_3_sample_to_s16(sample: &[u8; 3]) -> i16 {
    let as_u16 = Le16::from_bytes(*most_significant_bytes_le(sample)).get();
    range_shift(as_u16)
}

fn convert_s20_sample_to_s16(sample: &[u8; 4]) -> i16 {
    let i20_0_padded =
        u32::from(sample[0]) | u32::from(sample[1]) << 8 | u32::from(sample[2] & 0b1111) << 16;
    (i20_0_padded >> 4) as i16
}

fn convert_u20_sample_to_s16(sample: &[u8; 4]) -> i16 {
    let u20_0_padded =
        u32::from(sample[0]) | u32::from(sample[1]) << 8 | u32::from(sample[2] & 0b1111) << 16;
    let as_u16 = (u20_0_padded >> 4) as u16;
    range_shift(as_u16)
}

fn convert_s24_sample_to_s16(sample: &[u8; 4]) -> i16 {
    Le16::from_bytes(*most_significant_bytes_le(sample)).get() as i16
}

fn convert_u24_sample_to_s16(sample: &[u8; 4]) -> i16 {
    let as_u16 = Le16::from_bytes(*most_significant_bytes_le(sample)).get();
    range_shift(as_u16)
}

fn convert_s32_sample_to_s16(sample: &[u8; 4]) -> i16 {
    Le16::from_bytes(*most_significant_bytes_le(sample)).get() as i16
}

fn convert_u32_sample_to_s16(sample: &[u8; 4]) -> i16 {
    range_shift(Le16::from_bytes(*most_significant_bytes_le(sample)).get())
}

fn convert_float_sample_to_s16(sample: &[u8; 4]) -> i16 {
    (LeF32::from_bytes(*sample).get() * 32768.0) as i16
}

fn convert_float64_sample_to_s16(sample: &[u8; 8]) -> i16 {
    (LeF64::from_bytes(*sample).get() * 32768.0) as i16
}
