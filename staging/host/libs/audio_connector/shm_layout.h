//
// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <atomic>
#include <type_traits>

#include "common/libs/utils/cf_endian.h"

namespace cuttlefish {
// TODO (b/175151042): get these from the kernel headers when available

enum class AudioCommandType : uint32_t {
  /* jack control request types */
  VIRTIO_SND_R_JACK_INFO = 1,
  VIRTIO_SND_R_JACK_REMAP,

  /* PCM control request types */
  VIRTIO_SND_R_PCM_INFO = 0x0100,
  VIRTIO_SND_R_PCM_SET_PARAMS,
  VIRTIO_SND_R_PCM_PREPARE,
  VIRTIO_SND_R_PCM_RELEASE,
  VIRTIO_SND_R_PCM_START,
  VIRTIO_SND_R_PCM_STOP,

  /* channel map control request types */
  VIRTIO_SND_R_CHMAP_INFO = 0x0200,
};

enum class AudioStatus : uint32_t {
  /* common status codes */
  VIRTIO_SND_S_OK = 0x8000,
  VIRTIO_SND_S_BAD_MSG,
  VIRTIO_SND_S_NOT_SUPP,
  VIRTIO_SND_S_IO_ERR,
  // Not a virtio constant, but it's only used internally as an invalid value so
  // it's safe.
  NOT_SET = static_cast<uint32_t>(-1),
};

enum class AudioStreamDirection : uint32_t {
  VIRTIO_SND_D_OUTPUT = 0,
  VIRTIO_SND_D_INPUT
};

enum class AudioStreamFormat : uint8_t {
  /* analog formats (width / physical width) */
  VIRTIO_SND_PCM_FMT_IMA_ADPCM = 0, /*  4 /  4 bits */
  VIRTIO_SND_PCM_FMT_MU_LAW,        /*  8 /  8 bits */
  VIRTIO_SND_PCM_FMT_A_LAW,         /*  8 /  8 bits */
  VIRTIO_SND_PCM_FMT_S8,            /*  8 /  8 bits */
  VIRTIO_SND_PCM_FMT_U8,            /*  8 /  8 bits */
  VIRTIO_SND_PCM_FMT_S16,           /* 16 / 16 bits */
  VIRTIO_SND_PCM_FMT_U16,           /* 16 / 16 bits */
  VIRTIO_SND_PCM_FMT_S18_3,         /* 18 / 24 bits */
  VIRTIO_SND_PCM_FMT_U18_3,         /* 18 / 24 bits */
  VIRTIO_SND_PCM_FMT_S20_3,         /* 20 / 24 bits */
  VIRTIO_SND_PCM_FMT_U20_3,         /* 20 / 24 bits */
  VIRTIO_SND_PCM_FMT_S24_3,         /* 24 / 24 bits */
  VIRTIO_SND_PCM_FMT_U24_3,         /* 24 / 24 bits */
  VIRTIO_SND_PCM_FMT_S20,           /* 20 / 32 bits */
  VIRTIO_SND_PCM_FMT_U20,           /* 20 / 32 bits */
  VIRTIO_SND_PCM_FMT_S24,           /* 24 / 32 bits */
  VIRTIO_SND_PCM_FMT_U24,           /* 24 / 32 bits */
  VIRTIO_SND_PCM_FMT_S32,           /* 32 / 32 bits */
  VIRTIO_SND_PCM_FMT_U32,           /* 32 / 32 bits */
  VIRTIO_SND_PCM_FMT_FLOAT,         /* 32 / 32 bits */
  VIRTIO_SND_PCM_FMT_FLOAT64,       /* 64 / 64 bits */
  /* digital formats (width / physical width) */
  VIRTIO_SND_PCM_FMT_DSD_U8,         /*  8 /  8 bits */
  VIRTIO_SND_PCM_FMT_DSD_U16,        /* 16 / 16 bits */
  VIRTIO_SND_PCM_FMT_DSD_U32,        /* 32 / 32 bits */
  VIRTIO_SND_PCM_FMT_IEC958_SUBFRAME /* 32 / 32 bits */
};

/* supported PCM frame rates */
enum AudioStreamRate : uint8_t {
  VIRTIO_SND_PCM_RATE_5512 = 0,
  VIRTIO_SND_PCM_RATE_8000,
  VIRTIO_SND_PCM_RATE_11025,
  VIRTIO_SND_PCM_RATE_16000,
  VIRTIO_SND_PCM_RATE_22050,
  VIRTIO_SND_PCM_RATE_32000,
  VIRTIO_SND_PCM_RATE_44100,
  VIRTIO_SND_PCM_RATE_48000,
  VIRTIO_SND_PCM_RATE_64000,
  VIRTIO_SND_PCM_RATE_88200,
  VIRTIO_SND_PCM_RATE_96000,
  VIRTIO_SND_PCM_RATE_176400,
  VIRTIO_SND_PCM_RATE_192000,
  VIRTIO_SND_PCM_RATE_384000
};

struct virtio_snd_hdr {
  Le32 code;
};

struct virtio_snd_query_info {
  struct virtio_snd_hdr hdr;
  Le32 start_id;
  Le32 count;
  Le32 size;  // unused
};

struct virtio_snd_info {
  Le32 hda_fn_nid;
};

struct virtio_snd_pcm_info {
  struct virtio_snd_info hdr;
  Le32 features; /* 1 << VIRTIO_SND_PCM_F_XXX */
  Le64 formats;  /* 1 << VIRTIO_SND_PCM_FMT_XXX */
  Le64 rates;    /* 1 << VIRTIO_SND_PCM_RATE_XXX */
  uint8_t direction;
  uint8_t channels_min;
  uint8_t channels_max;

  uint8_t padding[5];
};

struct virtio_snd_pcm_hdr {
  struct virtio_snd_hdr hdr;
  Le32 stream_id;
};

struct virtio_snd_pcm_set_params {
  struct virtio_snd_pcm_hdr hdr;
  Le32 buffer_bytes;
  Le32 period_bytes;
  Le32 features; /* 1 << VIRTIO_SND_PCM_F_XXX */
  uint8_t channels;
  uint8_t format;
  uint8_t rate;
  uint8_t padding;
};

struct virtio_snd_pcm_xfer {
  Le32 stream_id;
};

struct virtio_snd_pcm_status {
  Le32 status;
  Le32 latency_bytes;
};

// Update this value when the msg layouts change
const uint32_t VIOS_VERSION = 1;

struct VioSConfig {
  uint32_t version;
  uint32_t jacks;
  uint32_t streams;
  uint32_t chmaps;
};

struct IoTransferMsg {
  virtio_snd_pcm_xfer io_xfer;
  uint32_t buffer_offset;
  uint32_t buffer_len;
};

struct IoStatusMsg {
  virtio_snd_pcm_status status;
  uint32_t buffer_offset;
  uint32_t consumed_length;
};

// Ensure all message structs have predictable sizes
#define ASSERT_VALID_MSG_TYPE(T, size) \
  static_assert(sizeof(T) == (size), #T " has the wrong size")
ASSERT_VALID_MSG_TYPE(virtio_snd_query_info, 16);
ASSERT_VALID_MSG_TYPE(virtio_snd_pcm_info, 32);
ASSERT_VALID_MSG_TYPE(virtio_snd_pcm_set_params, 24);
ASSERT_VALID_MSG_TYPE(virtio_snd_pcm_hdr, 8);
ASSERT_VALID_MSG_TYPE(IoTransferMsg, 12);
ASSERT_VALID_MSG_TYPE(IoStatusMsg, 16);
#undef ASSERT_VALID_MSG_TYPE

}  // namespace cuttlefish