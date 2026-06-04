// Copyright 2026, The Android Open Source Project
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

use std::io::BufWriter;
use std::io::Cursor;
use std::io::Write;

use image::ColorType;
use image::codecs::jpeg::JpegEncoder;
use v4l2r::PixelFormat;
use v4l2r::QueueType;
use v4l2r::bindings;
use v4l2r::bindings::v4l2_format;
use vhu_media::devices::CaptureDeviceFormat;
use virtio_media::ioctl::IoctlResult;

const WIDTH: u32 = 640;
const HEIGHT: u32 = 480;
const FRAME_RATE: u32 = 30;
const BYTES_PER_LINE: u32 = WIDTH * 3;
const PIXELFORMAT: u32 = PixelFormat::from_fourcc(b"MJPG").to_u32();
const BUFFER_SIZE: u32 = 9040;

const INPUTS: [bindings::v4l2_input; 1] = [bindings::v4l2_input {
    index: 0,
    name: *b"Default\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",
    type_: bindings::V4L2_INPUT_TYPE_CAMERA,
    ..unsafe { std::mem::zeroed() }
}];

pub struct SplaneFormat;

impl CaptureDeviceFormat for SplaneFormat {
    const QUEUE_TYPE: QueueType = QueueType::VideoCapture;
    const PIXELFORMAT: u32 = PIXELFORMAT;
    const BUFFER_SIZE: u32 = BUFFER_SIZE;
    const FRAME_RATE: u32 = FRAME_RATE;
    const WIDTH: u32 = WIDTH;
    const HEIGHT: u32 = HEIGHT;
    const INPUTS: &'static [bindings::v4l2_input] = &INPUTS;

    fn default_fmt(queue: QueueType) -> v4l2_format {
        let pix = bindings::v4l2_pix_format {
            width: WIDTH,
            height: HEIGHT,
            pixelformat: PIXELFORMAT,
            field: bindings::v4l2_field_V4L2_FIELD_NONE,
            bytesperline: BYTES_PER_LINE,
            sizeimage: BUFFER_SIZE,
            colorspace: bindings::v4l2_colorspace_V4L2_COLORSPACE_SRGB,
            ..Default::default()
        };

        v4l2_format {
            type_: queue as u32,
            fmt: bindings::v4l2_format__bindgen_ty_1 { pix },
        }
    }

    fn write_pattern<W: std::io::Write>(iteration: u64, sink: W) -> IoctlResult<()> {
        let buffer_size: u32 = BYTES_PER_LINE * HEIGHT;
        let mut rgb_buffer: Vec<u8> = vec![0; buffer_size as usize];
        for i in (0..buffer_size).step_by(3) {
            rgb_buffer[i as usize] = 0xffu8 * (iteration as u8 % 2);
            rgb_buffer[i as usize + 1] = 0x55u8 * (iteration as u8 % 3);
            rgb_buffer[i as usize + 2] = 0x10u8 * (iteration as u8 % 16);
        }
        // Compress buffer into a new JPEG buffer
        let mut compressed_buffer = Vec::new();
        {
            let mut encoder =
                JpegEncoder::new_with_quality(Cursor::new(&mut compressed_buffer), 95);
            encoder
                .encode(&rgb_buffer, WIDTH, HEIGHT, ColorType::Rgb8)
                .map_err(|_| libc::EIO)?;
        }
        let mut writer = BufWriter::new(sink);
        let _ = writer.write(&compressed_buffer).map_err(|_| libc::EIO)?;
        // Fill out the remaining of the buffer with zeros.
        for _ in compressed_buffer.len()..(BUFFER_SIZE as usize) {
            let _ = writer.write(&[0]).map_err(|_| libc::EIO)?;
        }

        Ok(())
    }

}
