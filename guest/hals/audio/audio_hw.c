/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This code was forked from device/generic/goldfish/audio/audio_hw.c
 *
 * At the time of forking, the code was identical except that a fallback
 * to a legacy HAL which does not use ALSA was removed, and the dependency
 * on libdl was also removed.
 */

#define LOG_TAG "audio_hw_generic"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

#include <log/log.h>
#include <cutils/list.h>
#include <cutils/str_parms.h>

#include <hardware/hardware.h>
#include <system/audio.h>
#include <hardware/audio.h>
#include <tinyalsa/asoundlib.h>

#define PCM_CARD 0
#define PCM_DEVICE 0


#define OUT_PERIOD_MS 15
#define OUT_PERIOD_COUNT 4

#define IN_PERIOD_MS 15
#define IN_PERIOD_COUNT 4

struct generic_audio_device {
    struct audio_hw_device device;          // Constant after init
    pthread_mutex_t lock;
    bool mic_mute;                          // Protected by this->lock
    struct mixer* mixer;                    // Protected by this->lock
    struct listnode out_streams;            // Record for output streams, protected by this->lock
    struct listnode in_streams;             // Record for input streams, protected by this->lock
    audio_patch_handle_t next_patch_handle; // Protected by this->lock
};

/* If not NULL, this is a pointer to the fallback module.
 * This really is the original goldfish audio device /dev/eac which we will use
 * if no alsa devices are detected.
 */
static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state);
static int adev_get_microphones(const audio_hw_device_t *dev,
                                struct audio_microphone_characteristic_t *mic_array,
                                size_t *mic_count);


typedef struct audio_vbuffer {
    pthread_mutex_t lock;
    uint8_t *  data;
    size_t     frame_size;
    size_t     frame_count;
    size_t     head;
    size_t     tail;
    size_t     live;
} audio_vbuffer_t;

static int audio_vbuffer_init (audio_vbuffer_t * audio_vbuffer, size_t frame_count,
                              size_t frame_size) {
    if (!audio_vbuffer) {
        return -EINVAL;
    }
    audio_vbuffer->frame_size = frame_size;
    audio_vbuffer->frame_count = frame_count;
    size_t bytes = frame_count * frame_size;
    audio_vbuffer->data = calloc(bytes, 1);
    if (!audio_vbuffer->data) {
        return -ENOMEM;
    }
    audio_vbuffer->head = 0;
    audio_vbuffer->tail = 0;
    audio_vbuffer->live = 0;
    pthread_mutex_init (&audio_vbuffer->lock, (const pthread_mutexattr_t *) NULL);
    return 0;
}

static int audio_vbuffer_destroy (audio_vbuffer_t * audio_vbuffer) {
    if (!audio_vbuffer) {
        return -EINVAL;
    }
    free(audio_vbuffer->data);
    pthread_mutex_destroy(&audio_vbuffer->lock);
    return 0;
}

static int audio_vbuffer_live (audio_vbuffer_t * audio_vbuffer) {
    if (!audio_vbuffer) {
        return -EINVAL;
    }
    pthread_mutex_lock (&audio_vbuffer->lock);
    int live = audio_vbuffer->live;
    pthread_mutex_unlock (&audio_vbuffer->lock);
    return live;
}

#define MIN(a,b) (((a)<(b))?(a):(b))
static size_t audio_vbuffer_write (audio_vbuffer_t * audio_vbuffer, const void * buffer, size_t frame_count) {
    size_t frames_written = 0;
    pthread_mutex_lock (&audio_vbuffer->lock);

    while (frame_count != 0) {
        int frames = 0;
        if (audio_vbuffer->live == 0 || audio_vbuffer->head > audio_vbuffer->tail) {
            frames = MIN(frame_count, audio_vbuffer->frame_count - audio_vbuffer->head);
        } else if (audio_vbuffer->head < audio_vbuffer->tail) {
            frames = MIN(frame_count, audio_vbuffer->tail - (audio_vbuffer->head));
        } else {
            // Full
            break;
        }
        memcpy(&audio_vbuffer->data[audio_vbuffer->head*audio_vbuffer->frame_size],
               &((uint8_t*)buffer)[frames_written*audio_vbuffer->frame_size],
               frames*audio_vbuffer->frame_size);
        audio_vbuffer->live += frames;
        frames_written += frames;
        frame_count -= frames;
        audio_vbuffer->head = (audio_vbuffer->head + frames) % audio_vbuffer->frame_count;
    }

    pthread_mutex_unlock (&audio_vbuffer->lock);
    return frames_written;
}

static size_t audio_vbuffer_read (audio_vbuffer_t * audio_vbuffer, void * buffer, size_t frame_count) {
    size_t frames_read = 0;
    pthread_mutex_lock (&audio_vbuffer->lock);

    while (frame_count != 0) {
        int frames = 0;
        if (audio_vbuffer->live == audio_vbuffer->frame_count ||
            audio_vbuffer->tail > audio_vbuffer->head) {
            frames = MIN(frame_count, audio_vbuffer->frame_count - audio_vbuffer->tail);
        } else if (audio_vbuffer->tail < audio_vbuffer->head) {
            frames = MIN(frame_count, audio_vbuffer->head - audio_vbuffer->tail);
        } else {
            break;
        }
        memcpy(&((uint8_t*)buffer)[frames_read*audio_vbuffer->frame_size],
               &audio_vbuffer->data[audio_vbuffer->tail*audio_vbuffer->frame_size],
               frames*audio_vbuffer->frame_size);
        audio_vbuffer->live -= frames;
        frames_read += frames;
        frame_count -= frames;
        audio_vbuffer->tail = (audio_vbuffer->tail + frames) % audio_vbuffer->frame_count;
    }

    pthread_mutex_unlock (&audio_vbuffer->lock);
    return frames_read;
}

struct generic_stream_out {
    struct audio_stream_out stream;                 // Constant after init
    pthread_mutex_t lock;
    struct generic_audio_device *dev;               // Constant after init
    uint32_t num_devices;                           // Protected by this->lock
    audio_devices_t devices[AUDIO_PATCH_PORTS_MAX]; // Protected by this->lock
    struct audio_config req_config;                 // Constant after init
    struct pcm_config pcm_config;                   // Constant after init
    audio_vbuffer_t buffer;                         // Constant after init

    // Time & Position Keeping
    bool standby;                      // Protected by this->lock
    uint64_t underrun_position;        // Protected by this->lock
    struct timespec underrun_time;     // Protected by this->lock
    uint64_t last_write_time_us;       // Protected by this->lock
    uint64_t frames_total_buffered;    // Protected by this->lock
    uint64_t frames_written;           // Protected by this->lock
    uint64_t frames_rendered;          // Protected by this->lock

    // Worker
    pthread_t worker_thread;          // Constant after init
    pthread_cond_t worker_wake;       // Protected by this->lock
    bool worker_standby;              // Protected by this->lock
    bool worker_exit;                 // Protected by this->lock

    audio_io_handle_t handle;          // Constant after init
    audio_patch_handle_t patch_handle; // Protected by this->dev->lock

    struct listnode stream_node;       // Protected by this->dev->lock
};

struct generic_stream_in {
    struct audio_stream_in stream;    // Constant after init
    pthread_mutex_t lock;
    struct generic_audio_device *dev; // Constant after init
    audio_devices_t device;           // Protected by this->lock
    struct audio_config req_config;   // Constant after init
    struct pcm *pcm;                  // Protected by this->lock
    struct pcm_config pcm_config;     // Constant after init
    int16_t *stereo_to_mono_buf;      // Protected by this->lock
    size_t stereo_to_mono_buf_size;   // Protected by this->lock
    audio_vbuffer_t buffer;           // Protected by this->lock

    // Time & Position Keeping
    bool standby;                     // Protected by this->lock
    int64_t standby_position;         // Protected by this->lock
    struct timespec standby_exit_time;// Protected by this->lock
    int64_t standby_frames_read;      // Protected by this->lock

    // Worker
    pthread_t worker_thread;          // Constant after init
    pthread_cond_t worker_wake;       // Protected by this->lock
    bool worker_standby;              // Protected by this->lock
    bool worker_exit;                 // Protected by this->lock

    audio_io_handle_t handle;          // Constant after init
    audio_patch_handle_t patch_handle; // Protected by this->dev->lock

    struct listnode stream_node;       // Protected by this->dev->lock
};

static struct pcm_config pcm_config_out = {
    .channels = 2,
    .rate = 0,
    .period_size = 0,
    .period_count = OUT_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
};

static struct pcm_config pcm_config_in = {
    .channels = 2,
    .rate = 0,
    .period_size = 0,
    .period_count = IN_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
};

static pthread_mutex_t adev_init_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned int audio_device_ref_count = 0;

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    struct generic_stream_out *out = (struct generic_stream_out *)stream;
    return out->req_config.sample_rate;
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return -ENOSYS;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    struct generic_stream_out *out = (struct generic_stream_out *)stream;
    int size = out->pcm_config.period_size *
                audio_stream_out_frame_size(&out->stream);

    return size;
}

static audio_channel_mask_t out_get_channels(const struct audio_stream *stream)
{
    struct generic_stream_out *out = (struct generic_stream_out *)stream;
    return out->req_config.channel_mask;
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    struct generic_stream_out *out = (struct generic_stream_out *)stream;

    return out->req_config.format;
}

static int out_set_format(struct audio_stream *stream, audio_format_t format)
{
    return -ENOSYS;
}

static int out_dump(const struct audio_stream *stream, int fd)
{
    struct generic_stream_out *out = (struct generic_stream_out *)stream;
    pthread_mutex_lock(&out->lock);
    dprintf(fd, "\tout_dump:\n"
                "\t\tsample rate: %u\n"
                "\t\tbuffer size: %zu\n"
                "\t\tchannel mask: %08x\n"
                "\t\tformat: %d\n"
                "\t\tdevice(s): ",
                out_get_sample_rate(stream),
                out_get_buffer_size(stream),
                out_get_channels(stream),
                out_get_format(stream));
    if (out->num_devices == 0) {
        dprintf(fd, "%08x\n", AUDIO_DEVICE_NONE);
    } else {
        for (uint32_t i = 0; i < out->num_devices; i++) {
            if (i != 0) {
                dprintf(fd, ", ");
            }
            dprintf(fd, "%08x", out->devices[i]);
        }
        dprintf(fd, "\n");
    }
    dprintf(fd, "\t\taudio dev: %p\n\n", out->dev);
    pthread_mutex_unlock(&out->lock);
    return 0;
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct str_parms *parms;
    char value[32];
    int success;
    int ret = -EINVAL;

    if (kvpairs == NULL || kvpairs[0] == 0) {
        return 0;
    }
    parms = str_parms_create_str(kvpairs);
    success = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING,
            value, sizeof(value));
    // As the hal version is 3.0, it must not use set parameters API to set audio devices.
    // Instead, it should use create_audio_patch API.
    assert(("Must not use set parameters API to set audio devices", success < 0));

    if (str_parms_has_key(parms, AUDIO_PARAMETER_STREAM_FORMAT)) {
        // match the return value of out_set_format
        ret = -ENOSYS;
    }

    str_parms_destroy(parms);

    if (ret == -EINVAL) {
        ALOGW("%s(), unsupported parameter %s", __func__, kvpairs);
        // There is not any key supported for set_parameters API.
        // Return error when there is non-null value passed in.
    }
    return ret;
}

static char * out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    struct generic_stream_out *out = (struct generic_stream_out *)stream;
    struct str_parms *query = str_parms_create_str(keys);
    char *str = NULL;
    char value[256];
    struct str_parms *reply = str_parms_create();
    int ret;
    bool get = false;

    ret = str_parms_get_str(query, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (ret >= 0) {
        pthread_mutex_lock(&out->lock);
        audio_devices_t device = AUDIO_DEVICE_NONE;
        for (uint32_t i = 0; i < out->num_devices; i++) {
            device |= out->devices[i];
        }
        str_parms_add_int(reply, AUDIO_PARAMETER_STREAM_ROUTING, device);
        pthread_mutex_unlock(&out->lock);
        get = true;
    }

    if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_FORMATS)) {
        value[0] = 0;
        strcat(value, "AUDIO_FORMAT_PCM_16_BIT");
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_FORMATS, value);
        get = true;
    }

    if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_FORMAT)) {
        value[0] = 0;
        strcat(value, "AUDIO_FORMAT_PCM_16_BIT");
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_FORMAT, value);
        get = true;
    }

    if (get) {
        str = str_parms_to_str(reply);
    }
    else {
        ALOGD("%s Unsupported paramter: %s", __FUNCTION__, keys);
    }

    str_parms_destroy(query);
    str_parms_destroy(reply);
    return str;
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    struct generic_stream_out *out = (struct generic_stream_out *)stream;
    return (out->pcm_config.period_size * 1000) / out->pcm_config.rate;
}

static int out_set_volume(struct audio_stream_out *stream, float left,
                          float right)
{
    return -ENOSYS;
}

static void *out_write_worker(void * args)
{
    struct generic_stream_out *out = (struct generic_stream_out *)args;
    struct pcm *pcm = NULL;
    uint8_t *buffer = NULL;
    int buffer_frames;
    int buffer_size;
    bool restart = false;
    bool shutdown = false;
    while (true) {
        pthread_mutex_lock(&out->lock);
        while (out->worker_standby || restart) {
            restart = false;
            if (pcm) {
                pcm_close(pcm); // Frees pcm
                pcm = NULL;
                free(buffer);
                buffer=NULL;
            }
            if (out->worker_exit) {
                break;
            }
            pthread_cond_wait(&out->worker_wake, &out->lock);
        }

        if (out->worker_exit) {
            if (!out->worker_standby) {
                ALOGE("Out worker not in standby before exiting");
            }
            shutdown = true;
        }

        while (!shutdown && audio_vbuffer_live(&out->buffer) == 0) {
            pthread_cond_wait(&out->worker_wake, &out->lock);
        }

        if (shutdown) {
            pthread_mutex_unlock(&out->lock);
            break;
        }

        if (!pcm) {
            pcm = pcm_open(PCM_CARD, PCM_DEVICE,
                          PCM_OUT | PCM_MONOTONIC, &out->pcm_config);
            if (!pcm_is_ready(pcm)) {
                ALOGE("pcm_open(out) failed: %s: channels %d format %d rate %d",
                  pcm_get_error(pcm),
                  out->pcm_config.channels,
                  out->pcm_config.format,
                  out->pcm_config.rate
                   );
                pthread_mutex_unlock(&out->lock);
                break;
            }
            buffer_frames = out->pcm_config.period_size;
            buffer_size = pcm_frames_to_bytes(pcm, buffer_frames);
            buffer = malloc(buffer_size);
            if (!buffer) {
                ALOGE("could not allocate write buffer");
                pthread_mutex_unlock(&out->lock);
                break;
            }
        }
        int frames = audio_vbuffer_read(&out->buffer, buffer, buffer_frames);
        pthread_mutex_unlock(&out->lock);
        int ret = pcm_write(pcm, buffer, pcm_frames_to_bytes(pcm, frames));
        if (ret != 0) {
            ALOGE("pcm_write failed %s", pcm_get_error(pcm));
            restart = true;
        }
    }
    if (buffer) {
        free(buffer);
    }

    return NULL;
}

// Call with in->lock held
static void get_current_output_position(struct generic_stream_out *out,
                                       uint64_t * position,
                                       struct timespec * timestamp) {
    struct timespec curtime = { .tv_sec = 0, .tv_nsec = 0 };
    clock_gettime(CLOCK_MONOTONIC, &curtime);
    const int64_t now_us = (curtime.tv_sec * 1000000000LL + curtime.tv_nsec) / 1000;
    if (timestamp) {
        *timestamp = curtime;
    }
    int64_t position_since_underrun;
    if (out->standby) {
        position_since_underrun = 0;
    } else {
        const int64_t first_us = (out->underrun_time.tv_sec * 1000000000LL +
                                  out->underrun_time.tv_nsec) / 1000;
        position_since_underrun = (now_us - first_us) *
                out_get_sample_rate(&out->stream.common) /
                1000000;
        if (position_since_underrun < 0) {
            position_since_underrun = 0;
        }
    }
    *position = out->underrun_position + position_since_underrun;

    // The device will reuse the same output stream leading to periods of
    // underrun.
    if (*position > out->frames_written) {
        ALOGW("Not supplying enough data to HAL, expected position %" PRIu64 " , only wrote "
              "%" PRIu64,
              *position, out->frames_written);

        *position = out->frames_written;
        out->underrun_position = *position;
        out->underrun_time = curtime;
        out->frames_total_buffered = 0;
    }
}


static ssize_t out_write(struct audio_stream_out *stream, const void *buffer,
                         size_t bytes)
{
    struct generic_stream_out *out = (struct generic_stream_out *)stream;
    const size_t frames =  bytes / audio_stream_out_frame_size(stream);

    pthread_mutex_lock(&out->lock);

    if (out->worker_standby) {
        out->worker_standby = false;
    }

    uint64_t current_position;
    struct timespec current_time;

    get_current_output_position(out, &current_position, &current_time);
    const uint64_t now_us = (current_time.tv_sec * 1000000000LL +
                             current_time.tv_nsec) / 1000;
    if (out->standby) {
        out->standby = false;
        out->underrun_time = current_time;
        out->frames_rendered = 0;
        out->frames_total_buffered = 0;
    }

    size_t frames_written = audio_vbuffer_write(&out->buffer, buffer, frames);
    pthread_cond_signal(&out->worker_wake);

    /* Implementation just consumes bytes if we start getting backed up */
    out->frames_written += frames;
    out->frames_rendered += frames;
    out->frames_total_buffered += frames;

    // We simulate the audio device blocking when it's write buffers become
    // full.

    // At the beginning or after an underrun, try to fill up the vbuffer.
    // This will be throttled by the PlaybackThread
    int frames_sleep = out->frames_total_buffered < out->buffer.frame_count ? 0 : frames;

    uint64_t sleep_time_us = frames_sleep * 1000000LL /
                            out_get_sample_rate(&stream->common);

    // If the write calls are delayed, subtract time off of the sleep to
    // compensate
    uint64_t time_since_last_write_us = now_us - out->last_write_time_us;
    if (time_since_last_write_us < sleep_time_us) {
        sleep_time_us -= time_since_last_write_us;
    } else {
        sleep_time_us = 0;
    }
    out->last_write_time_us = now_us + sleep_time_us;

    pthread_mutex_unlock(&out->lock);

    if (sleep_time_us > 0) {
        usleep(sleep_time_us);
    }

    if (frames_written < frames) {
        ALOGW("Hardware backing HAL too slow, could only write %zu of %zu frames", frames_written, frames);
    }

    /* Always consume all bytes */
    return bytes;
}

static int out_get_presentation_position(const struct audio_stream_out *stream,
                                   uint64_t *frames, struct timespec *timestamp)

{
    if (stream == NULL || frames == NULL || timestamp == NULL) {
        return -EINVAL;
    }
    struct generic_stream_out *out = (struct generic_stream_out *)stream;

    pthread_mutex_lock(&out->lock);
    get_current_output_position(out, frames, timestamp);
    pthread_mutex_unlock(&out->lock);

    return 0;
}

static int out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames)
{
    if (stream == NULL || dsp_frames == NULL) {
        return -EINVAL;
    }
    struct generic_stream_out *out = (struct generic_stream_out *)stream;
    pthread_mutex_lock(&out->lock);
    *dsp_frames = out->frames_rendered;
    pthread_mutex_unlock(&out->lock);
    return 0;
}

// Must be called with out->lock held
static void do_out_standby(struct generic_stream_out *out)
{
    int frames_sleep = 0;
    uint64_t sleep_time_us = 0;
    if (out->standby) {
        return;
    }
    while (true) {
        get_current_output_position(out, &out->underrun_position, NULL);
        frames_sleep = out->frames_written - out->underrun_position;

        if (frames_sleep == 0) {
            break;
        }

        sleep_time_us = frames_sleep * 1000000LL /
                        out_get_sample_rate(&out->stream.common);

        pthread_mutex_unlock(&out->lock);
        usleep(sleep_time_us);
        pthread_mutex_lock(&out->lock);
    }
    out->worker_standby = true;
    out->standby = true;
}

static int out_standby(struct audio_stream *stream)
{
    struct generic_stream_out *out = (struct generic_stream_out *)stream;
    pthread_mutex_lock(&out->lock);
    do_out_standby(out);
    pthread_mutex_unlock(&out->lock);
    return 0;
}

static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    // out_add_audio_effect is a no op
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    // out_remove_audio_effect is a no op
    return 0;
}

static int out_get_next_write_timestamp(const struct audio_stream_out *stream,
                                        int64_t *timestamp)
{
    return -ENOSYS;
}

static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    struct generic_stream_in *in = (struct generic_stream_in *)stream;
    return in->req_config.sample_rate;
}

static int in_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return -ENOSYS;
}

static int refine_output_parameters(uint32_t *sample_rate, audio_format_t *format, audio_channel_mask_t *channel_mask)
{
    static const uint32_t sample_rates [] = {8000,11025,16000,22050,24000,32000,
                                            44100,48000};
    static const int sample_rates_count = sizeof(sample_rates)/sizeof(uint32_t);
    bool inval = false;
    if (*format != AUDIO_FORMAT_PCM_16_BIT) {
        *format = AUDIO_FORMAT_PCM_16_BIT;
        inval = true;
    }

    int channel_count = popcount(*channel_mask);
    if (channel_count != 1 && channel_count != 2) {
        *channel_mask = AUDIO_CHANNEL_IN_STEREO;
        inval = true;
    }

    int i;
    for (i = 0; i < sample_rates_count; i++) {
        if (*sample_rate < sample_rates[i]) {
            *sample_rate = sample_rates[i];
            inval=true;
            break;
        }
        else if (*sample_rate == sample_rates[i]) {
            break;
        }
        else if (i == sample_rates_count-1) {
            // Cap it to the highest rate we support
            *sample_rate = sample_rates[i];
            inval=true;
        }
    }

    if (inval) {
        return -EINVAL;
    }
    return 0;
}

static int refine_input_parameters(uint32_t *sample_rate, audio_format_t *format, audio_channel_mask_t *channel_mask)
{
    static const uint32_t sample_rates [] = {8000, 11025, 16000, 22050, 44100, 48000};
    static const int sample_rates_count = sizeof(sample_rates)/sizeof(uint32_t);
    bool inval = false;
    // Only PCM_16_bit is supported. If this is changed, stereo to mono drop
    // must be fixed in in_read
    if (*format != AUDIO_FORMAT_PCM_16_BIT) {
        *format = AUDIO_FORMAT_PCM_16_BIT;
        inval = true;
    }

    int channel_count = popcount(*channel_mask);
    if (channel_count != 1 && channel_count != 2) {
        *channel_mask = AUDIO_CHANNEL_IN_STEREO;
        inval = true;
    }

    int i;
    for (i = 0; i < sample_rates_count; i++) {
        if (*sample_rate < sample_rates[i]) {
            *sample_rate = sample_rates[i];
            inval=true;
            break;
        }
        else if (*sample_rate == sample_rates[i]) {
            break;
        }
        else if (i == sample_rates_count-1) {
            // Cap it to the highest rate we support
            *sample_rate = sample_rates[i];
            inval=true;
        }
    }

    if (inval) {
        return -EINVAL;
    }
    return 0;
}

static int check_input_parameters(uint32_t sample_rate, audio_format_t format,
                                  audio_channel_mask_t channel_mask)
{
    return refine_input_parameters(&sample_rate, &format, &channel_mask);
}

static size_t get_input_buffer_size(uint32_t sample_rate, audio_format_t format,
                                    audio_channel_mask_t channel_mask)
{
    size_t size;
    int channel_count = popcount(channel_mask);
    if (check_input_parameters(sample_rate, format, channel_mask) != 0)
        return 0;

    size = sample_rate*IN_PERIOD_MS/1000;
    // Audioflinger expects audio buffers to be multiple of 16 frames
    size = ((size + 15) / 16) * 16;
    size *= sizeof(short) * channel_count;

    return size;
}


static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    struct generic_stream_in *in = (struct generic_stream_in *)stream;
    int size = get_input_buffer_size(in->req_config.sample_rate,
                                 in->req_config.format,
                                 in->req_config.channel_mask);

    return size;
}

static audio_channel_mask_t in_get_channels(const struct audio_stream *stream)
{
    struct generic_stream_in *in = (struct generic_stream_in *)stream;
    return in->req_config.channel_mask;
}

static audio_format_t in_get_format(const struct audio_stream *stream)
{
    struct generic_stream_in *in = (struct generic_stream_in *)stream;
    return in->req_config.format;
}

static int in_set_format(struct audio_stream *stream, audio_format_t format)
{
    return -ENOSYS;
}

static int in_dump(const struct audio_stream *stream, int fd)
{
    struct generic_stream_in *in = (struct generic_stream_in *)stream;

    pthread_mutex_lock(&in->lock);
    dprintf(fd, "\tin_dump:\n"
                "\t\tsample rate: %u\n"
                "\t\tbuffer size: %zu\n"
                "\t\tchannel mask: %08x\n"
                "\t\tformat: %d\n"
                "\t\tdevice: %08x\n"
                "\t\taudio dev: %p\n\n",
                in_get_sample_rate(stream),
                in_get_buffer_size(stream),
                in_get_channels(stream),
                in_get_format(stream),
                in->device,
                in->dev);
    pthread_mutex_unlock(&in->lock);
    return 0;
}

static int in_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct str_parms *parms;
    char value[32];
    int success;
    int ret = -EINVAL;

    if (kvpairs == NULL || kvpairs[0] == 0) {
        return 0;
    }
    parms = str_parms_create_str(kvpairs);
    success = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING,
            value, sizeof(value));
    // As the hal version is 3.0, it must not use set parameters API to set audio device.
    // Instead, it should use create_audio_patch API.
    assert(("Must not use set parameters API to set audio devices", success < 0));

    if (str_parms_has_key(parms, AUDIO_PARAMETER_STREAM_FORMAT)) {
        // match the return value of in_set_format
        ret = -ENOSYS;
    }

    str_parms_destroy(parms);

    if (ret == -EINVAL) {
        ALOGW("%s(), unsupported parameter %s", __func__, kvpairs);
        // There is not any key supported for set_parameters API.
        // Return error when there is non-null value passed in.
    }
    return ret;
}

static char * in_get_parameters(const struct audio_stream *stream,
                                const char *keys)
{
    struct generic_stream_in *in = (struct generic_stream_in *)stream;
    struct str_parms *query = str_parms_create_str(keys);
    char *str = NULL;
    char value[256];
    struct str_parms *reply = str_parms_create();
    int ret;
    bool get = false;

    ret = str_parms_get_str(query, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (ret >= 0) {
        str_parms_add_int(reply, AUDIO_PARAMETER_STREAM_ROUTING, in->device);
        get = true;
    }

    if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_FORMATS)) {
        value[0] = 0;
        strcat(value, "AUDIO_FORMAT_PCM_16_BIT");
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_FORMATS, value);
        get = true;
    }

    if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_FORMAT)) {
        value[0] = 0;
        strcat(value, "AUDIO_FORMAT_PCM_16_BIT");
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_FORMAT, value);
        get = true;
    }

    if (get) {
        str = str_parms_to_str(reply);
    }
    else {
        ALOGD("%s Unsupported paramter: %s", __FUNCTION__, keys);
    }

    str_parms_destroy(query);
    str_parms_destroy(reply);
    return str;
}

static int in_set_gain(struct audio_stream_in *stream, float gain)
{
    // in_set_gain is a no op
    return 0;
}

// Call with in->lock held
static void get_current_input_position(struct generic_stream_in *in,
                                       int64_t * position,
                                       struct timespec * timestamp) {
    struct timespec t = { .tv_sec = 0, .tv_nsec = 0 };
    clock_gettime(CLOCK_MONOTONIC, &t);
    const int64_t now_us = (t.tv_sec * 1000000000LL + t.tv_nsec) / 1000;
    if (timestamp) {
        *timestamp = t;
    }
    int64_t position_since_standby;
    if (in->standby) {
        position_since_standby = 0;
    } else {
        const int64_t first_us = (in->standby_exit_time.tv_sec * 1000000000LL +
                                  in->standby_exit_time.tv_nsec) / 1000;
        position_since_standby = (now_us - first_us) *
                in_get_sample_rate(&in->stream.common) /
                1000000;
        if (position_since_standby < 0) {
            position_since_standby = 0;
        }
    }
    *position = in->standby_position + position_since_standby;
}

// Must be called with in->lock held
static void do_in_standby(struct generic_stream_in *in)
{
    if (in->standby) {
        return;
    }
    in->worker_standby = true;
    get_current_input_position(in, &in->standby_position, NULL);
    in->standby = true;
}

static int in_standby(struct audio_stream *stream)
{
    struct generic_stream_in *in = (struct generic_stream_in *)stream;
    pthread_mutex_lock(&in->lock);
    do_in_standby(in);
    pthread_mutex_unlock(&in->lock);
    return 0;
}

static void *in_read_worker(void * args)
{
    struct generic_stream_in *in = (struct generic_stream_in *)args;
    struct pcm *pcm = NULL;
    uint8_t *buffer = NULL;
    size_t buffer_frames;
    int buffer_size;

    bool restart = false;
    bool shutdown = false;
    while (true) {
        pthread_mutex_lock(&in->lock);
        while (in->worker_standby || restart) {
            restart = false;
            if (pcm) {
                pcm_close(pcm); // Frees pcm
                pcm = NULL;
                free(buffer);
                buffer=NULL;
            }
            if (in->worker_exit) {
                break;
            }
            pthread_cond_wait(&in->worker_wake, &in->lock);
        }

        if (in->worker_exit) {
            if (!in->worker_standby) {
                ALOGE("In worker not in standby before exiting");
            }
            shutdown = true;
        }
        if (shutdown) {
            pthread_mutex_unlock(&in->lock);
            break;
        }
        if (!pcm) {
            pcm = pcm_open(PCM_CARD, PCM_DEVICE,
                          PCM_IN | PCM_MONOTONIC, &in->pcm_config);
            if (!pcm_is_ready(pcm)) {
                ALOGE("pcm_open(in) failed: %s: channels %d format %d rate %d",
                  pcm_get_error(pcm),
                  in->pcm_config.channels,
                  in->pcm_config.format,
                  in->pcm_config.rate
                   );
                pthread_mutex_unlock(&in->lock);
                break;
            }
            buffer_frames = in->pcm_config.period_size;
            buffer_size = pcm_frames_to_bytes(pcm, buffer_frames);
            buffer = malloc(buffer_size);
            if (!buffer) {
                ALOGE("could not allocate worker read buffer");
                pthread_mutex_unlock(&in->lock);
                break;
            }
        }
        pthread_mutex_unlock(&in->lock);
        int ret = pcm_read(pcm, buffer, pcm_frames_to_bytes(pcm, buffer_frames));
        if (ret != 0) {
            ALOGW("pcm_read failed %s", pcm_get_error(pcm));
            restart = true;
            continue;
        }

        pthread_mutex_lock(&in->lock);
        size_t frames_written = audio_vbuffer_write(&in->buffer, buffer, buffer_frames);
        pthread_mutex_unlock(&in->lock);

        if (frames_written != buffer_frames) {
            ALOGW("in_read_worker only could write %zu / %zu frames", frames_written, buffer_frames);
        }
    }
    if (buffer) {
        free(buffer);
    }
    return NULL;
}

static ssize_t in_read(struct audio_stream_in *stream, void* buffer,
                       size_t bytes)
{
    struct generic_stream_in *in = (struct generic_stream_in *)stream;
    struct generic_audio_device *adev = in->dev;
    const size_t frames =  bytes / audio_stream_in_frame_size(stream);
    bool mic_mute = false;
    size_t read_bytes = 0;

    adev_get_mic_mute(&adev->device, &mic_mute);
    pthread_mutex_lock(&in->lock);

    if (in->worker_standby) {
        in->worker_standby = false;
    }
    pthread_cond_signal(&in->worker_wake);

    int64_t current_position;
    struct timespec current_time;

    get_current_input_position(in, &current_position, &current_time);
    if (in->standby) {
        in->standby = false;
        in->standby_exit_time = current_time;
        in->standby_frames_read = 0;
    }

    const int64_t frames_available = current_position - in->standby_position - in->standby_frames_read;
    assert(frames_available >= 0);

    const size_t frames_wait = ((uint64_t)frames_available > frames) ? 0 : frames - frames_available;

    int64_t sleep_time_us  = frames_wait * 1000000LL /
                             in_get_sample_rate(&stream->common);

    pthread_mutex_unlock(&in->lock);

    if (sleep_time_us > 0) {
        usleep(sleep_time_us);
    }

    pthread_mutex_lock(&in->lock);
    int read_frames = 0;
    if (in->standby) {
        ALOGW("Input put to sleep while read in progress");
        goto exit;
    }
    in->standby_frames_read += frames;

    if (popcount(in->req_config.channel_mask) == 1 &&
        in->pcm_config.channels == 2) {
        // Need to resample to mono
        if (in->stereo_to_mono_buf_size < bytes*2) {
            in->stereo_to_mono_buf = realloc(in->stereo_to_mono_buf,
                                             bytes*2);
            if (!in->stereo_to_mono_buf) {
                ALOGE("Failed to allocate stereo_to_mono_buff");
                goto exit;
            }
        }

        read_frames = audio_vbuffer_read(&in->buffer, in->stereo_to_mono_buf, frames);

        // Currently only pcm 16 is supported.
        uint16_t *src = (uint16_t *)in->stereo_to_mono_buf;
        uint16_t *dst = (uint16_t *)buffer;
        size_t i;
        // Resample stereo 16 to mono 16 by dropping one channel.
        // The stereo stream is interleaved L-R-L-R
        for (i = 0; i < frames; i++) {
            *dst = *src;
            src += 2;
            dst += 1;
        }
    } else {
        read_frames = audio_vbuffer_read(&in->buffer, buffer, frames);
    }

exit:
    read_bytes = read_frames*audio_stream_in_frame_size(stream);

    if (mic_mute) {
        read_bytes = 0;
    }

    if (read_bytes < bytes) {
        memset (&((uint8_t *)buffer)[read_bytes], 0, bytes-read_bytes);
    }

    pthread_mutex_unlock(&in->lock);

    return bytes;
}

static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream)
{
    return 0;
}

static int in_get_capture_position(const struct audio_stream_in *stream,
                                int64_t *frames, int64_t *time)
{
    struct generic_stream_in *in = (struct generic_stream_in *)stream;
    pthread_mutex_lock(&in->lock);
    struct timespec current_time;
    get_current_input_position(in, frames, &current_time);
    *time = (current_time.tv_sec * 1000000000LL + current_time.tv_nsec);
    pthread_mutex_unlock(&in->lock);
    return 0;
}

static int in_get_active_microphones(const struct audio_stream_in *stream,
                                     struct audio_microphone_characteristic_t *mic_array,
                                     size_t *mic_count)
{
    return adev_get_microphones(NULL, mic_array, mic_count);
}

static int in_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    // in_add_audio_effect is a no op
    return 0;
}

static int in_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    // in_add_audio_effect is a no op
    return 0;
}

static int adev_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out,
                                   const char *address __unused)
{
    struct generic_audio_device *adev = (struct generic_audio_device *)dev;
    struct generic_stream_out *out;
    int ret = 0;

    if (refine_output_parameters(&config->sample_rate, &config->format, &config->channel_mask)) {
        ALOGE("Error opening output stream format %d, channel_mask %04x, sample_rate %u",
              config->format, config->channel_mask, config->sample_rate);
        ret = -EINVAL;
        goto error;
    }

    out = (struct generic_stream_out *)calloc(1, sizeof(struct generic_stream_out));

    if (!out)
        return -ENOMEM;

    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;
    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.write = out_write;
    out->stream.get_render_position = out_get_render_position;
    out->stream.get_presentation_position = out_get_presentation_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;

    out->handle = handle;

    pthread_mutex_init(&out->lock, (const pthread_mutexattr_t *) NULL);
    out->dev = adev;
    // Only 1 device is expected despite the argument being named 'devices'
    out->num_devices = 1;
    out->devices[0] = devices;
    memcpy(&out->req_config, config, sizeof(struct audio_config));
    memcpy(&out->pcm_config, &pcm_config_out, sizeof(struct pcm_config));
    out->pcm_config.rate = config->sample_rate;
    out->pcm_config.period_size = out->pcm_config.rate*OUT_PERIOD_MS/1000;

    out->standby = true;
    out->underrun_position = 0;
    out->underrun_time.tv_sec = 0;
    out->underrun_time.tv_nsec = 0;
    out->last_write_time_us = 0;
    out->frames_total_buffered = 0;
    out->frames_written = 0;
    out->frames_rendered = 0;

    ret = audio_vbuffer_init(&out->buffer,
                      out->pcm_config.period_size*out->pcm_config.period_count,
                      out->pcm_config.channels *
                      pcm_format_to_bits(out->pcm_config.format) >> 3);
    if (ret == 0) {
        pthread_cond_init(&out->worker_wake, NULL);
        out->worker_standby = true;
        out->worker_exit = false;
        pthread_create(&out->worker_thread, NULL, out_write_worker, out);

    }

    pthread_mutex_lock(&adev->lock);
    list_add_tail(&adev->out_streams, &out->stream_node);
    pthread_mutex_unlock(&adev->lock);

    *stream_out = &out->stream;

error:

    return ret;
}

// This must be called with adev->lock held.
struct generic_stream_out *get_stream_out_by_io_handle_l(
        struct generic_audio_device *adev, audio_io_handle_t handle) {
    struct listnode *node;

    list_for_each(node, &adev->out_streams) {
        struct generic_stream_out *out = node_to_item(
                node, struct generic_stream_out, stream_node);
        if (out->handle == handle) {
            return out;
        }
    }
    return NULL;
}

static void adev_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out *stream)
{
    struct generic_stream_out *out = (struct generic_stream_out *)stream;
    pthread_mutex_lock(&out->lock);
    do_out_standby(out);

    out->worker_exit = true;
    pthread_cond_signal(&out->worker_wake);
    pthread_mutex_unlock(&out->lock);

    pthread_join(out->worker_thread, NULL);
    pthread_mutex_destroy(&out->lock);
    audio_vbuffer_destroy(&out->buffer);

    struct generic_audio_device *adev = (struct generic_audio_device *) dev;
    pthread_mutex_lock(&adev->lock);
    list_remove(&out->stream_node);
    pthread_mutex_unlock(&adev->lock);
    free(stream);
}

static int adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    return 0;
}

static char * adev_get_parameters(const struct audio_hw_device *dev,
                                  const char *keys)
{
    return strdup("");
}

static int adev_init_check(const struct audio_hw_device *dev)
{
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
    // adev_set_voice_volume is a no op (simulates phones)
    return 0;
}

static int adev_set_master_volume(struct audio_hw_device *dev, float volume)
{
    return -ENOSYS;
}

static int adev_get_master_volume(struct audio_hw_device *dev, float *volume)
{
    return -ENOSYS;
}

static int adev_set_master_mute(struct audio_hw_device *dev, bool muted)
{
    return -ENOSYS;
}

static int adev_get_master_mute(struct audio_hw_device *dev, bool *muted)
{
    return -ENOSYS;
}

static int adev_set_mode(struct audio_hw_device *dev, audio_mode_t mode)
{
    // adev_set_mode is a no op (simulates phones)
    return 0;
}

static int adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
    struct generic_audio_device *adev = (struct generic_audio_device *)dev;
    pthread_mutex_lock(&adev->lock);
    adev->mic_mute = state;
    pthread_mutex_unlock(&adev->lock);
    return 0;
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state)
{
    struct generic_audio_device *adev = (struct generic_audio_device *)dev;
    pthread_mutex_lock(&adev->lock);
    *state = adev->mic_mute;
    pthread_mutex_unlock(&adev->lock);
    return 0;
}


static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev,
                                         const struct audio_config *config)
{
    return get_input_buffer_size(config->sample_rate, config->format, config->channel_mask);
}

// This must be called with adev->lock held.
struct generic_stream_in *get_stream_in_by_io_handle_l(
        struct generic_audio_device *adev, audio_io_handle_t handle) {
    struct listnode *node;

    list_for_each(node, &adev->in_streams) {
        struct generic_stream_in *in = node_to_item(
                node, struct generic_stream_in, stream_node);
        if (in->handle == handle) {
            return in;
        }
    }
    return NULL;
}

static void adev_close_input_stream(struct audio_hw_device *dev,
                                   struct audio_stream_in *stream)
{
    struct generic_stream_in *in = (struct generic_stream_in *)stream;
    pthread_mutex_lock(&in->lock);
    do_in_standby(in);

    in->worker_exit = true;
    pthread_cond_signal(&in->worker_wake);
    pthread_mutex_unlock(&in->lock);
    pthread_join(in->worker_thread, NULL);

    if (in->stereo_to_mono_buf != NULL) {
        free(in->stereo_to_mono_buf);
        in->stereo_to_mono_buf_size = 0;
    }

    pthread_mutex_destroy(&in->lock);
    audio_vbuffer_destroy(&in->buffer);

    struct generic_audio_device *adev = (struct generic_audio_device *) dev;
    pthread_mutex_lock(&adev->lock);
    list_remove(&in->stream_node);
    pthread_mutex_unlock(&adev->lock);
    free(stream);
}


static int adev_open_input_stream(struct audio_hw_device *dev,
                                  audio_io_handle_t handle,
                                  audio_devices_t devices,
                                  struct audio_config *config,
                                  struct audio_stream_in **stream_in,
                                  audio_input_flags_t flags __unused,
                                  const char *address __unused,
                                  audio_source_t source __unused)
{
    struct generic_audio_device *adev = (struct generic_audio_device *)dev;
    struct generic_stream_in *in;
    int ret = 0;
    if (refine_input_parameters(&config->sample_rate, &config->format, &config->channel_mask)) {
        ALOGE("Error opening input stream format %d, channel_mask %04x, sample_rate %u",
              config->format, config->channel_mask, config->sample_rate);
        ret = -EINVAL;
        goto error;
    }

    in = (struct generic_stream_in *)calloc(1, sizeof(struct generic_stream_in));
    if (!in) {
        ret = -ENOMEM;
        goto error;
    }

    in->stream.common.get_sample_rate = in_get_sample_rate;
    in->stream.common.set_sample_rate = in_set_sample_rate;         // no op
    in->stream.common.get_buffer_size = in_get_buffer_size;
    in->stream.common.get_channels = in_get_channels;
    in->stream.common.get_format = in_get_format;
    in->stream.common.set_format = in_set_format;                   // no op
    in->stream.common.standby = in_standby;
    in->stream.common.dump = in_dump;
    in->stream.common.set_parameters = in_set_parameters;
    in->stream.common.get_parameters = in_get_parameters;
    in->stream.common.add_audio_effect = in_add_audio_effect;       // no op
    in->stream.common.remove_audio_effect = in_remove_audio_effect; // no op
    in->stream.set_gain = in_set_gain;                              // no op
    in->stream.read = in_read;
    in->stream.get_input_frames_lost = in_get_input_frames_lost;    // no op
    in->stream.get_capture_position = in_get_capture_position;
    in->stream.get_active_microphones = in_get_active_microphones;

    pthread_mutex_init(&in->lock, (const pthread_mutexattr_t *) NULL);
    in->dev = adev;
    in->device = devices;
    memcpy(&in->req_config, config, sizeof(struct audio_config));
    memcpy(&in->pcm_config, &pcm_config_in, sizeof(struct pcm_config));
    in->pcm_config.rate = config->sample_rate;
    in->pcm_config.period_size = in->pcm_config.rate*IN_PERIOD_MS/1000;

    in->stereo_to_mono_buf = NULL;
    in->stereo_to_mono_buf_size = 0;

    in->standby = true;
    in->standby_position = 0;
    in->standby_exit_time.tv_sec = 0;
    in->standby_exit_time.tv_nsec = 0;
    in->standby_frames_read = 0;

    ret = audio_vbuffer_init(&in->buffer,
                      in->pcm_config.period_size*in->pcm_config.period_count,
                      in->pcm_config.channels *
                      pcm_format_to_bits(in->pcm_config.format) >> 3);
    if (ret == 0) {
        pthread_cond_init(&in->worker_wake, NULL);
        in->worker_standby = true;
        in->worker_exit = false;
        pthread_create(&in->worker_thread, NULL, in_read_worker, in);
    }
    in->handle = handle;

    pthread_mutex_lock(&adev->lock);
    list_add_tail(&adev->in_streams, &in->stream_node);
    pthread_mutex_unlock(&adev->lock);

    *stream_in = &in->stream;

error:
    return ret;
}


static int adev_dump(const audio_hw_device_t *dev, int fd)
{
    return 0;
}

static int adev_get_microphones(const audio_hw_device_t *dev,
                                struct audio_microphone_characteristic_t *mic_array,
                                size_t *mic_count)
{
    if (mic_count == NULL) {
        return -ENOSYS;
    }

    if (*mic_count == 0) {
        *mic_count = 1;
        return 0;
    }

    if (mic_array == NULL) {
        return -ENOSYS;
    }

    strncpy(mic_array->device_id, "mic_goldfish", AUDIO_MICROPHONE_ID_MAX_LEN - 1);
    mic_array->device = AUDIO_DEVICE_IN_BUILTIN_MIC;
    strncpy(mic_array->address, AUDIO_BOTTOM_MICROPHONE_ADDRESS,
            AUDIO_DEVICE_MAX_ADDRESS_LEN - 1);
    memset(mic_array->channel_mapping, AUDIO_MICROPHONE_CHANNEL_MAPPING_UNUSED,
           sizeof(mic_array->channel_mapping));
    mic_array->location = AUDIO_MICROPHONE_LOCATION_UNKNOWN;
    mic_array->group = 0;
    mic_array->index_in_the_group = 0;
    mic_array->sensitivity = AUDIO_MICROPHONE_SENSITIVITY_UNKNOWN;
    mic_array->max_spl = AUDIO_MICROPHONE_SPL_UNKNOWN;
    mic_array->min_spl = AUDIO_MICROPHONE_SPL_UNKNOWN;
    mic_array->directionality = AUDIO_MICROPHONE_DIRECTIONALITY_UNKNOWN;
    mic_array->num_frequency_responses = 0;
    mic_array->geometric_location.x = AUDIO_MICROPHONE_COORDINATE_UNKNOWN;
    mic_array->geometric_location.y = AUDIO_MICROPHONE_COORDINATE_UNKNOWN;
    mic_array->geometric_location.z = AUDIO_MICROPHONE_COORDINATE_UNKNOWN;
    mic_array->orientation.x = AUDIO_MICROPHONE_COORDINATE_UNKNOWN;
    mic_array->orientation.y = AUDIO_MICROPHONE_COORDINATE_UNKNOWN;
    mic_array->orientation.z = AUDIO_MICROPHONE_COORDINATE_UNKNOWN;

    *mic_count = 1;
    return 0;
}

static int adev_create_audio_patch(struct audio_hw_device *dev,
                                   unsigned int num_sources,
                                   const struct audio_port_config *sources,
                                   unsigned int num_sinks,
                                   const struct audio_port_config *sinks,
                                   audio_patch_handle_t *handle) {
    if (num_sources != 1 || num_sinks == 0 || num_sinks > AUDIO_PATCH_PORTS_MAX) {
        return -EINVAL;
    }

    if (sources[0].type == AUDIO_PORT_TYPE_DEVICE) {
        // If source is a device, the number of sinks should be 1.
        if (num_sinks != 1 || sinks[0].type != AUDIO_PORT_TYPE_MIX) {
            return -EINVAL;
        }
    } else if (sources[0].type == AUDIO_PORT_TYPE_MIX) {
        // If source is a mix, all sinks should be device.
        for (unsigned int i = 0; i < num_sinks; i++) {
            if (sinks[i].type != AUDIO_PORT_TYPE_DEVICE) {
                ALOGE("%s() invalid sink type %#x for mix source", __func__, sinks[i].type);
                return -EINVAL;
            }
        }
    } else {
        // All other cases are invalid.
        return -EINVAL;
    }

    struct generic_audio_device* adev = (struct generic_audio_device*) dev;
    int ret = 0;
    bool generatedPatchHandle = false;
    pthread_mutex_lock(&adev->lock);
    if (*handle == AUDIO_PATCH_HANDLE_NONE) {
        *handle = ++adev->next_patch_handle;
        generatedPatchHandle = true;
    }

    // Only handle patches for mix->devices and device->mix case.
    if (sources[0].type == AUDIO_PORT_TYPE_DEVICE) {
        struct generic_stream_in *in =
                get_stream_in_by_io_handle_l(adev, sinks[0].ext.mix.handle);
        if (in == NULL) {
            ALOGE("%s()can not find stream with handle(%d)", __func__, sources[0].ext.mix.handle);
            ret = -EINVAL;
            goto error;
        }

        // Check if the patch handle match the recorded one if a valid patch handle is passed.
        if (!generatedPatchHandle && in->patch_handle != *handle) {
            ALOGE("%s() the patch handle(%d) does not match recorded one(%d) for stream "
                  "with handle(%d) when creating audio patch for device->mix",
                  __func__, *handle, in->patch_handle, in->handle);
            ret = -EINVAL;
            goto error;
        }
        pthread_mutex_lock(&in->lock);
        in->device = sources[0].ext.device.type;
        pthread_mutex_unlock(&in->lock);
        in->patch_handle = *handle;
    } else {
        struct generic_stream_out *out =
                get_stream_out_by_io_handle_l(adev, sources[0].ext.mix.handle);
        if (out == NULL) {
            ALOGE("%s()can not find stream with handle(%d)", __func__, sources[0].ext.mix.handle);
            ret = -EINVAL;
            goto error;
        }

        // Check if the patch handle match the recorded one if a valid patch handle is passed.
        if (!generatedPatchHandle && out->patch_handle != *handle) {
            ALOGE("%s() the patch handle(%d) does not match recorded one(%d) for stream "
                  "with handle(%d) when creating audio patch for mix->device",
                  __func__, *handle, out->patch_handle, out->handle);
            ret = -EINVAL;
            pthread_mutex_unlock(&out->lock);
            goto error;
        }
        pthread_mutex_lock(&out->lock);
        for (out->num_devices = 0; out->num_devices < num_sinks; out->num_devices++) {
            out->devices[out->num_devices] = sinks[out->num_devices].ext.device.type;
        }
        pthread_mutex_unlock(&out->lock);
        out->patch_handle = *handle;
    }

error:
    if (ret != 0 && generatedPatchHandle) {
        *handle = AUDIO_PATCH_HANDLE_NONE;
    }
    pthread_mutex_unlock(&adev->lock);
    return 0;
}

// This must be called with adev->lock held.
struct generic_stream_out *get_stream_out_by_patch_handle_l(
        struct generic_audio_device *adev, audio_patch_handle_t patch_handle) {
    struct listnode *node;

    list_for_each(node, &adev->out_streams) {
        struct generic_stream_out *out = node_to_item(
                node, struct generic_stream_out, stream_node);
        if (out->patch_handle == patch_handle) {
            return out;
        }
    }
    return NULL;
}

// This must be called with adev->lock held.
struct generic_stream_in *get_stream_in_by_patch_handle_l(
        struct generic_audio_device *adev, audio_patch_handle_t patch_handle) {
    struct listnode *node;

    list_for_each(node, &adev->in_streams) {
        struct generic_stream_in *in = node_to_item(
                node, struct generic_stream_in, stream_node);
        if (in->patch_handle == patch_handle) {
            return in;
        }
    }
    return NULL;
}

static int adev_release_audio_patch(struct audio_hw_device *dev,
                                    audio_patch_handle_t patch_handle) {
    struct generic_audio_device *adev = (struct generic_audio_device *) dev;

    pthread_mutex_lock(&adev->lock);
    struct generic_stream_out *out = get_stream_out_by_patch_handle_l(adev, patch_handle);
    if (out != NULL) {
        pthread_mutex_lock(&out->lock);
        out->num_devices = 0;
        memset(out->devices, 0, sizeof(out->devices));
        pthread_mutex_unlock(&out->lock);
        out->patch_handle = AUDIO_PATCH_HANDLE_NONE;
        pthread_mutex_unlock(&adev->lock);
        return 0;
    }
    struct generic_stream_in *in = get_stream_in_by_patch_handle_l(adev, patch_handle);
    if (in != NULL) {
        pthread_mutex_lock(&in->lock);
        in->device = AUDIO_DEVICE_NONE;
        pthread_mutex_unlock(&in->lock);
        in->patch_handle = AUDIO_PATCH_HANDLE_NONE;
        pthread_mutex_unlock(&adev->lock);
        return 0;
    }

    pthread_mutex_unlock(&adev->lock);
    ALOGW("%s() cannot find stream for patch handle: %d", __func__, patch_handle);
    return -EINVAL;
}

static int adev_close(hw_device_t *dev)
{
    struct generic_audio_device *adev = (struct generic_audio_device *)dev;
    int ret = 0;
    if (!adev)
        return 0;

    pthread_mutex_lock(&adev_init_lock);

    if (audio_device_ref_count == 0) {
        ALOGE("adev_close called when ref_count 0");
        ret = -EINVAL;
        goto error;
    }

    if ((--audio_device_ref_count) == 0) {
        if (adev->mixer) {
            mixer_close(adev->mixer);
        }
        free(adev);
    }

error:
    pthread_mutex_unlock(&adev_init_lock);
    return ret;
}

static int adev_open(const hw_module_t* module, const char* name,
                     hw_device_t** device)
{
    static struct generic_audio_device *adev;

    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0)
        return -EINVAL;

    pthread_mutex_lock(&adev_init_lock);
    if (audio_device_ref_count != 0) {
        *device = &adev->device.common;
        audio_device_ref_count++;
        ALOGV("%s: returning existing instance of adev", __func__);
        ALOGV("%s: exit", __func__);
        goto unlock;
    }
    adev = calloc(1, sizeof(struct generic_audio_device));

    pthread_mutex_init(&adev->lock, (const pthread_mutexattr_t *) NULL);

    adev->device.common.tag = HARDWARE_DEVICE_TAG;
    adev->device.common.version = AUDIO_DEVICE_API_VERSION_3_0;
    adev->device.common.module = (struct hw_module_t *) module;
    adev->device.common.close = adev_close;

    adev->device.init_check = adev_init_check;               // no op
    adev->device.set_voice_volume = adev_set_voice_volume;   // no op
    adev->device.set_master_volume = adev_set_master_volume; // no op
    adev->device.get_master_volume = adev_get_master_volume; // no op
    adev->device.set_master_mute = adev_set_master_mute;     // no op
    adev->device.get_master_mute = adev_get_master_mute;     // no op
    adev->device.set_mode = adev_set_mode;                   // no op
    adev->device.set_mic_mute = adev_set_mic_mute;
    adev->device.get_mic_mute = adev_get_mic_mute;
    adev->device.set_parameters = adev_set_parameters;       // no op
    adev->device.get_parameters = adev_get_parameters;       // no op
    adev->device.get_input_buffer_size = adev_get_input_buffer_size;
    adev->device.open_output_stream = adev_open_output_stream;
    adev->device.close_output_stream = adev_close_output_stream;
    adev->device.open_input_stream = adev_open_input_stream;
    adev->device.close_input_stream = adev_close_input_stream;
    adev->device.dump = adev_dump;
    adev->device.get_microphones = adev_get_microphones;
    adev->device.create_audio_patch = adev_create_audio_patch;
    adev->device.release_audio_patch = adev_release_audio_patch;

    *device = &adev->device.common;

    adev->next_patch_handle = AUDIO_PATCH_HANDLE_NONE;
    list_init(&adev->out_streams);
    list_init(&adev->in_streams);

    adev->mixer = mixer_open(PCM_CARD);
    struct mixer_ctl *ctl;

    // Set default mixer ctls
    // Enable channels and set volume
    for (int i = 0; i < (int)mixer_get_num_ctls(adev->mixer); i++) {
        ctl = mixer_get_ctl(adev->mixer, i);
        ALOGD("mixer %d name %s", i, mixer_ctl_get_name(ctl));
        if (!strcmp(mixer_ctl_get_name(ctl), "Master Playback Volume") ||
            !strcmp(mixer_ctl_get_name(ctl), "Capture Volume")) {
            for (int z = 0; z < (int)mixer_ctl_get_num_values(ctl); z++) {
                ALOGD("set ctl %d to %d", z, 100);
                mixer_ctl_set_percent(ctl, z, 100);
            }
            continue;
        }
        if (!strcmp(mixer_ctl_get_name(ctl), "Master Playback Switch") ||
            !strcmp(mixer_ctl_get_name(ctl), "Capture Switch")) {
            for (int z = 0; z < (int)mixer_ctl_get_num_values(ctl); z++) {
                ALOGD("set ctl %d to %d", z, 1);
                mixer_ctl_set_value(ctl, z, 1);
            }
            continue;
        }
    }

    audio_device_ref_count++;

unlock:
    pthread_mutex_unlock(&adev_init_lock);
    return 0;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "Generic audio HW HAL",
        .author = "The Android Open Source Project",
        .methods = &hal_module_methods,
    },
};
