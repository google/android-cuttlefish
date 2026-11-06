#![allow(unused)]

use anyhow::{ensure, Context, Result};
use num_enum::{IntoPrimitive, TryFromPrimitive};
use zerocopy::byteorder::little_endian::{U16 as Le16, U32 as Le32, U64 as Le64};
use zerocopy::{FromBytes, FromZeros, Immutable, IntoBytes};

#[derive(Copy, Clone, Debug, FromBytes, Immutable, IntoBytes)]
#[repr(C)]
pub struct VirtioSndConfig {
    pub jacks: Le32,
    pub streams: Le32,
    pub chmaps: Le32,
    pub controls: Le32,
}

#[derive(Clone, Copy, Debug, PartialEq, IntoPrimitive, TryFromPrimitive)]
#[repr(u32)]
pub enum VirtioSndRequest {
    /* jack control request types */
    JackInfo = 1,
    JackRemap,

    /* PCM control request types */
    PcmInfo = 0x0100,
    PcmSetParams,
    PcmPrepare,
    PcmRelease,
    PcmStart,
    PcmStop,

    /* channel map control request types */
    ChmapInfo = 0x0200,

    /* control element request types */
    CtlInfo = 0x0300,
    CtlEnumItems,
    CtlRead,
    CtlWrite,
    CtlTlvRead,
    CtlTlvWrite,
    CtlTlvCommand,
}

impl TryFrom<Le32> for VirtioSndRequest {
    type Error = anyhow::Error;

    fn try_from(value: Le32) -> Result<Self> {
        Ok(Self::try_from(value.get())?)
    }
}

impl Into<Le32> for VirtioSndRequest {
    fn into(self) -> Le32 {
        Le32::new(self as u32)
    }
}

/* jack event types */
#[derive(Clone, Copy, Debug, PartialEq, IntoPrimitive, TryFromPrimitive)]
#[repr(u32)]
pub enum VirtioSndEventCode {
    JackConnected = 0x1000,
    JackDisconnected,
    PcmPeriodElapsed = 0x1100,
    PcmXrun,
    CtlNotify = 0x1200,
}

/* common status codes */
#[derive(Clone, Copy, Debug, PartialEq, IntoPrimitive, TryFromPrimitive)]
#[repr(u32)]
pub enum VirtioSndStatus {
    Ok = 0x8000,
    BadMsg,
    NotSupp,
    IoErr,
}

impl Into<Le32> for VirtioSndStatus {
    fn into(self) -> Le32 {
        Le32::new(self as u32)
    }
}

/* a common header */
#[derive(Copy, Clone, Debug, FromBytes, Immutable, IntoBytes)]
#[repr(C)]
pub struct VirtioSndHdr {
    pub code: Le32,
}

/* an event notification */
#[derive(Copy, Clone, Debug, FromBytes, Immutable, IntoBytes)]
#[repr(C)]
pub struct VirtioSndEvent {
    pub hdr: VirtioSndHdr,
    pub data: Le32,
}

#[derive(Clone, Copy, Debug, IntoPrimitive, TryFromPrimitive)]
#[repr(u8)]
pub enum VirtioSndDirection {
    Output = 0,
    Input,
}

#[derive(Copy, Clone, Debug, FromBytes, Immutable, IntoBytes)]
#[repr(C)]
pub struct VirtioSndQueryInfo {
    pub hdr: VirtioSndHdr,
    pub start_id: Le32,
    pub count: Le32,
    pub size: Le32,
}

#[derive(Copy, Clone, Debug, FromBytes, Immutable, IntoBytes)]
#[repr(C)]
pub struct VirtioSndInfo {
    pub hda_fn_nid: Le32,
}

#[derive(Copy, Clone, Debug, FromBytes, Immutable, IntoBytes)]
#[repr(C)]
pub struct virtio_snd_jack_hdr {
    pub hdr: VirtioSndHdr,
    pub jack_id: Le32,
}

/* supported jack features */
pub const VIRTIO_SND_JACK_F_REMAP: u8 = 0;

#[derive(Copy, Clone, Debug, FromBytes, Immutable, IntoBytes)]
#[repr(C)]
pub struct VirtioSndJackInfo {
    pub hdr: VirtioSndInfo,
    pub features: Le32, /* 1 << VIRTIO_SND_JACK_F_XXX */
    pub hda_reg_defconf: Le32,
    pub hda_reg_caps: Le32,
    pub connected: u8,

    pub padding: [u8; 7],
}

#[derive(Copy, Clone, Debug, FromBytes, Immutable, IntoBytes)]
#[repr(C)]
pub struct VirtioSndJackRemap {
    pub hdr: virtio_snd_jack_hdr, /* .code = VirtioSndRequest::JackRemap */
    pub association: Le32,
    pub sequence: Le32,
}

#[derive(Copy, Clone, Debug, FromBytes, Immutable, IntoBytes)]
#[repr(C)]
pub struct VirtioSndPcmHdr {
    pub hdr: VirtioSndHdr,
    pub stream_id: Le32,
}

/* supported PCM stream features */
#[derive(Clone, Copy, Debug, PartialEq, IntoPrimitive, TryFromPrimitive)]
#[repr(u8)]
pub enum VirtioSndPcmFeature {
    ShmemHost = 0,
    ShmemGuest,
    MsgPolling,
    EvtShmemPeriods,
    EvtXruns,
}

/* supported PCM sample formats */
#[derive(Clone, Copy, Debug, Default, PartialEq, IntoPrimitive, TryFromPrimitive)]
#[repr(u8)]
pub enum VirtioSndPcmFormat {
    /* analog formats (width / physical width) */
    #[default]
    ImaAdpcm = 0, /*  4 /  4 bits */
    MuLaw,   /*  8 /  8 bits */
    Alaw,    /*  8 /  8 bits */
    S8,      /*  8 /  8 bits */
    U8,      /*  8 /  8 bits */
    S16,     /* 16 / 16 bits */
    U16,     /* 16 / 16 bits */
    S18_3,   /* 18 / 24 bits */
    U18_3,   /* 18 / 24 bits */
    S20_3,   /* 20 / 24 bits */
    U20_3,   /* 20 / 24 bits */
    S24_3,   /* 24 / 24 bits */
    U24_3,   /* 24 / 24 bits */
    S20,     /* 20 / 32 bits */
    U20,     /* 20 / 32 bits */
    S24,     /* 24 / 32 bits */
    U24,     /* 24 / 32 bits */
    S32,     /* 32 / 32 bits */
    U32,     /* 32 / 32 bits */
    Float,   /* 32 / 32 bits */
    Float64, /* 64 / 64 bits */
    /* digital formats (width / physical width) */
    DsdU8,          /*  8 /  8 bits */
    DsdU16,         /* 16 / 16 bits */
    DsdU32,         /* 32 / 32 bits */
    Iec958Subframe, /* 32 / 32 bits */
}

pub fn bits_per_sample(format: VirtioSndPcmFormat) -> usize {
    match format {
        VirtioSndPcmFormat::ImaAdpcm => 4,
        VirtioSndPcmFormat::MuLaw => 8,
        VirtioSndPcmFormat::Alaw => 8,
        VirtioSndPcmFormat::S8 => 8,
        VirtioSndPcmFormat::U8 => 8,
        VirtioSndPcmFormat::S16 => 16,
        VirtioSndPcmFormat::U16 => 16,
        VirtioSndPcmFormat::S18_3 => 24,
        VirtioSndPcmFormat::U18_3 => 24,
        VirtioSndPcmFormat::S20_3 => 24,
        VirtioSndPcmFormat::U20_3 => 24,
        VirtioSndPcmFormat::S24_3 => 24,
        VirtioSndPcmFormat::U24_3 => 24,
        VirtioSndPcmFormat::S20 => 32,
        VirtioSndPcmFormat::U20 => 32,
        VirtioSndPcmFormat::S24 => 32,
        VirtioSndPcmFormat::U24 => 32,
        VirtioSndPcmFormat::S32 => 32,
        VirtioSndPcmFormat::U32 => 32,
        VirtioSndPcmFormat::Float => 32,
        VirtioSndPcmFormat::Float64 => 64,
        VirtioSndPcmFormat::DsdU8 => 8,
        VirtioSndPcmFormat::DsdU16 => 16,
        VirtioSndPcmFormat::DsdU32 => 32,
        VirtioSndPcmFormat::Iec958Subframe => 32,
    }
}

/* supported PCM frame rates */
#[derive(Clone, Copy, Debug, PartialEq, IntoPrimitive, TryFromPrimitive)]
#[repr(u8)]
pub enum VirtioSndPcmRate {
    R5512 = 0,
    R8000,
    R11025,
    R16000,
    R22050,
    R32000,
    R44100,
    R48000,
    R64000,
    R88200,
    R96000,
    R176400,
    R192000,
    R384000,
}

impl VirtioSndPcmRate {
    pub fn rate(self) -> u64 {
        match self {
            VirtioSndPcmRate::R5512 => 5512,
            VirtioSndPcmRate::R8000 => 8000,
            VirtioSndPcmRate::R11025 => 11025,
            VirtioSndPcmRate::R16000 => 16000,
            VirtioSndPcmRate::R22050 => 22050,
            VirtioSndPcmRate::R32000 => 32000,
            VirtioSndPcmRate::R44100 => 44100,
            VirtioSndPcmRate::R48000 => 48000,
            VirtioSndPcmRate::R64000 => 64000,
            VirtioSndPcmRate::R88200 => 88200,
            VirtioSndPcmRate::R96000 => 96000,
            VirtioSndPcmRate::R176400 => 176400,
            VirtioSndPcmRate::R192000 => 192000,
            VirtioSndPcmRate::R384000 => 384000,
        }
    }
}

#[derive(Copy, Clone, Debug, FromBytes, Immutable, IntoBytes)]
#[repr(C)]
pub struct VirtioSndPcmInfo {
    pub hdr: VirtioSndInfo,
    pub features: Le32, /* 1 << VirtioSndPcmFeature_XXX */
    pub formats: Le64,  /* 1 << VirtioSndPcmFormat_XXX */
    pub rates: Le64,    /* 1 << VirtioSndPcmRate_XXX */
    pub direction: u8,
    pub channels_min: u8,
    pub channels_max: u8,

    pub padding: [u8; 5],
}

#[derive(Copy, Clone, Debug, FromBytes, Immutable, IntoBytes)]
#[repr(C)]
pub struct VirtioSndPcmSetParams {
    pub hdr: VirtioSndPcmHdr, /* .code = VirtioSndRequest::PcmSetParams */
    pub buffer_bytes: Le32,
    pub period_bytes: Le32,
    pub features: Le32, /* 1 << VirtioSndPcmFeature_XXX */
    pub channels: u8,
    pub format: u8,
    pub rate: u8,

    pub padding: u8,
}

/* an I/O header */
#[derive(Copy, Clone, Debug, FromBytes, Immutable, IntoBytes)]
#[repr(C)]
pub struct VirtioSndPcmXfer {
    pub stream_id: Le32,
}

/* an I/O status */
#[derive(Copy, Clone, Debug, FromBytes, Immutable, IntoBytes)]
#[repr(C)]
pub struct VirtioSndPcmStatus {
    pub status: Le32,
    pub latency_bytes: Le32,
}

/* standard channel position definition */
#[derive(Clone, Copy, Debug, PartialEq, IntoPrimitive, TryFromPrimitive)]
#[repr(u8)]
pub enum VirtioSndChmap {
    None = 0,
    Silent,
    Mono,
    FrontLeft,
    FrontRight,
    RearLeft,
    RearRight,
    FrontCenter,
    LowFreq,
    SideLeft,
    SideRight,
    RearCenter,
    FrontLeftCenter,
    FrontRightCenter,
    RearLeftCenter,
    RearRightCenter,
    FrontLeftWide,
    FrontRightWide,
    FrontLeftHigh,
    FrontCenterHigh,
    FrontRightHigh,
    TopCenter,
    TopFrontLeft,
    TopFrontRight,
    TopFrontCenter,
    TopRearLeft,
    TopRearRight,
    TopRearCenter,
    TopFrontLeftCenter,
    TopFrontRightCenter,
    TopSideLeft,
    TopSideRight,
    LeftLowFreq,
    RightLowFreq,
    BottomCenter,
    BottomLeftCenter,
    BottomRifhtCenter,
}

/* maximum possible number of channels */
pub const VIRTIO_SND_CHMAP_MAX_SIZE: usize = 18;

#[derive(Copy, Clone, Debug, FromBytes, Immutable, IntoBytes)]
#[repr(C)]
pub struct VirtioSndChmapInfo {
    pub hdr: VirtioSndInfo,
    pub direction: u8,
    pub channels: u8,
    pub positions: [u8; VIRTIO_SND_CHMAP_MAX_SIZE],
}

#[derive(Copy, Clone, Debug, FromBytes, Immutable, IntoBytes)]
#[repr(C)]
pub struct VirtioSndCtlHdr {
    pub hdr: VirtioSndHdr,
    pub control_id: Le32,
}

#[derive(Clone, Copy, Debug, PartialEq, IntoPrimitive, TryFromPrimitive)]
#[repr(u32)]
pub enum VirtioSndCtlRole {
    Undefined = 0,
    Volume,
    Mute,
    Gain,
}

#[derive(Clone, Copy, Debug, PartialEq, IntoPrimitive, TryFromPrimitive)]
#[repr(u32)]
pub enum VirtioSndCtlType {
    Boolean = 0,
    Integer,
    Integer64,
    Enumerated,
    Bytes,
    Iec95,
}

#[derive(Clone, Copy, Debug, PartialEq)]
#[repr(u32)]
pub enum VirtioSndCtlAccess {
    Read = 0,
    Write,
    Volatile,
    Inactive,
    TlvRead,
    TlvWrite,
    TlvCommand,
}

#[derive(Copy, Clone, Debug, FromBytes, Immutable, IntoBytes)]
#[repr(C)]
pub struct VirtioSndCtlInfo {
    pub hdr: VirtioSndInfo,
    pub role: Le32,
    pub type_: Le32,
    pub access: Le32, /* 1 << VirtioSndCtlAccess_XXX */
    pub count: Le32,
    pub index: Le32,
    pub name: [u8; 44],
    /* one of VirtioSndCtlInfoInteger, VirtioSndCtlInfoInteger64, VirtioSndCtlInfoEnumerated*/
    pub value: [u8; 24],
}

#[derive(Copy, Clone, Debug, FromBytes, Immutable, IntoBytes)]
#[repr(C)]
pub struct VirtioSndCtlInfoInteger {
    pub min: Le32,
    pub max: Le32,
    pub step: Le32,
}

#[derive(Copy, Clone, Debug, FromBytes, Immutable, IntoBytes)]
#[repr(C)]
pub struct VirtioSndCtlInfoInteger64 {
    pub min: Le64,
    pub max: Le64,
    pub step: Le64,
}

#[derive(Copy, Clone, Debug, FromBytes, Immutable, IntoBytes)]
#[repr(C)]
pub struct VirtioSndCtlInfoEnumerated {
    pub items: Le32,
}

#[derive(Copy, Clone, Debug, FromBytes, Immutable, IntoBytes)]
#[repr(C)]
pub struct VirtioSndCtlEnumItem {
    pub item: [u8; 64],
}

#[derive(Copy, Clone, Debug, FromBytes, Immutable, IntoBytes)]
#[repr(C)]
pub struct VirtioSndCtlIec958 {
    pub status: [u8; 24],      /* AES/Iec958 channel status bits */
    pub subcode: [u8; 147],    /* AES/Iec958 subcode bits */
    pub pad: u8,               /* nothing */
    pub dig_subframe: [u8; 4], /* AES/Iec958 subframe bits */
}

#[derive(Copy, Clone, Debug, FromBytes, Immutable, IntoBytes)]
#[repr(C)]
pub struct VirtioSndCtlValue {
    /* union of [Le32; 128], [Le64; 64], [Le32; 128], [u8; 512], VirtioSndCtlIec958 */
    pub value: [u8; 512],
}

#[derive(Copy, Clone, Debug, FromBytes, Immutable, IntoBytes)]
#[repr(C)]
pub struct VirtioSndCtlTlv {
    pub type_: Le32,
    pub length: Le32,
    /* value: [Le32; length], */
}

#[derive(Clone, Copy, Debug, PartialEq)]
#[repr(u32)]
pub enum VirtioSndCtlEvtMask {
    Value = 0,
    Info,
    Tlv,
}

#[derive(Copy, Clone, Debug, FromBytes, Immutable, IntoBytes)]
#[repr(C)]
pub struct VirtioSndCtlEvent {
    pub hdr: VirtioSndHdr, /* .code = VIRTIO_SND_EVT_CTL_NOTIFY */
    pub control_id: Le16,
    pub mask: Le16, /* 1 << VIRTIO_SND_CTL_EVT_MASK_XXX */
}

pub fn validate_pcm_parameters(
    params: &VirtioSndPcmSetParams,
    pcm_info: &VirtioSndPcmInfo,
) -> Result<()> {
    let unsupported_features = params.features.get() & !pcm_info.features.get();
    ensure!(
        unsupported_features == 0u32,
        "Unsupported features: {:#032b}",
        unsupported_features
    );

    ensure!(
        params.channels >= pcm_info.channels_min && params.channels <= pcm_info.channels_max,
        "Unsupported number of channels: {}",
        params.channels
    );

    ensure!(
        (1u64 << params.format) & pcm_info.formats.get() != 0,
        "Unsupported format: {}",
        params.format
    );

    ensure!(
        (1u64 << params.rate) & pcm_info.rates.get() != 0,
        "Unsupported frame rate: {}",
        params.rate
    );

    ensure!(
        params.buffer_bytes % params.period_bytes == 0,
        "Buffer bytes not a multiple of period bytes: {} / {}",
        params.buffer_bytes,
        params.period_bytes
    );

    // bits_per_sample can only fail if the format is invalid, but the format has already been
    // validated against the pcm config, so this will result in a 0 if and only if the pcm config
    // includes an invalid format.
    let frame_size_bits =
        params.channels as u32 * bits_per_sample(params.format.try_into()?) as u32;
    let period_bits = 8 * params.period_bytes.get();
    ensure!(
        period_bits % frame_size_bits == 0,
        "Period bytes is not a multiple of frame size: {} bits / {} bits",
        period_bits,
        frame_size_bits
    );

    let guest_and_host_shmem = (1u32 << VirtioSndPcmFeature::ShmemGuest as u8)
        | (1u32 << VirtioSndPcmFeature::ShmemHost as u8);
    ensure!(
        guest_and_host_shmem & params.features != guest_and_host_shmem,
        "Requested SHMEM_GUEST and SHMEM_HOST simultaneously"
    );

    Ok(())
}
