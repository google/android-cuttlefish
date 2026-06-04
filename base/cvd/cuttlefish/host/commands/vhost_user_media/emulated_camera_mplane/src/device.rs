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
use std::io::Write;

use v4l2r::PixelFormat;
use v4l2r::QueueType;
use v4l2r::bindings;
use v4l2r::bindings::v4l2_format;
use vhu_media::devices::CaptureDeviceFormat;
use virtio_media::ioctl::IoctlResult;

const WIDTH: u32 = 640;
const HEIGHT: u32 = 480;
const FRAME_RATE: u32 = 30;
const PIXELFORMAT: u32 = PixelFormat::from_fourcc(b"YU12").to_u32();
const BUFFER_SIZE: u32 = WIDTH * HEIGHT * 3 / 2;

const INPUTS: [bindings::v4l2_input; 1] = [bindings::v4l2_input {
    index: 0,
    name: *b"Default\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",
    type_: bindings::V4L2_INPUT_TYPE_CAMERA,
    ..unsafe { std::mem::zeroed() }
}];

pub struct MplaneFormat;

impl CaptureDeviceFormat for MplaneFormat {
    const QUEUE_TYPE: QueueType = QueueType::VideoCaptureMplane;
    const PIXELFORMAT: u32 = PIXELFORMAT;
    const BUFFER_SIZE: u32 = BUFFER_SIZE;
    const FRAME_RATE: u32 = FRAME_RATE;
    const WIDTH: u32 = WIDTH;
    const HEIGHT: u32 = HEIGHT;
    const INPUTS: &'static [bindings::v4l2_input] = &INPUTS;

    fn default_fmt(queue: QueueType) -> v4l2_format {
        let pix_mp = bindings::v4l2_pix_format_mplane {
            width: WIDTH,
            height: HEIGHT,
            pixelformat: PIXELFORMAT,
            field: bindings::v4l2_field_V4L2_FIELD_NONE,
            colorspace: bindings::v4l2_colorspace_V4L2_COLORSPACE_SRGB,
            num_planes: 3,
            plane_fmt: [
                bindings::v4l2_plane_pix_format {
                    sizeimage: WIDTH * HEIGHT,
                    bytesperline: WIDTH,
                    ..Default::default()
                },
                bindings::v4l2_plane_pix_format {
                    sizeimage: WIDTH * HEIGHT / 4,
                    bytesperline: WIDTH / 2,
                    ..Default::default()
                },
                bindings::v4l2_plane_pix_format {
                    sizeimage: WIDTH * HEIGHT / 4,
                    bytesperline: WIDTH / 2,
                    ..Default::default()
                },
                Default::default(),
                Default::default(),
                Default::default(),
                Default::default(),
                Default::default(),
            ],
            ..Default::default()
        };

        v4l2_format {
            type_: queue as u32,
            fmt: bindings::v4l2_format__bindgen_ty_1 { pix_mp },
        }
    }

    fn write_pattern<W: std::io::Write>(iteration: u64, sink: W) -> IoctlResult<()> {
        let mut writer = BufWriter::new(sink);
        let y = (iteration % 256) as u8;
        let u = ((iteration + 64) % 256) as u8;
        let v = ((iteration + 128) % 256) as u8;
        for _ in 0..(WIDTH * HEIGHT) {
            writer.write_all(&[y]).map_err(|_| libc::EIO)?;
        }
        for _ in 0..(WIDTH * HEIGHT / 4) {
            writer.write_all(&[u]).map_err(|_| libc::EIO)?;
        }
        for _ in 0..(WIDTH * HEIGHT / 4) {
            writer.write_all(&[v]).map_err(|_| libc::EIO)?;
        }
        Ok(())
    }

}
