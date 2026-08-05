# V4L2 Stream Proxy Host Tool

This directory contains the `v4l2_stream_proxy` vhost-user device. To test this device, you can use the provided helper script to start a video stream on the host, which can then be proxied by the `cvd` tool from the host to the guest.

The test helper script is located at:
`tools/testutils/ffmpeg_v4l2_stream_proxy.sh` (relative to the repository root).

## Prerequisites

The script requires `ffmpeg` (and `ffprobe` if streaming from a file) to be installed on the host system.

```bash
sudo apt install ffmpeg
```

## Usage

The script creates a FIFO (named pipe) and runs `ffmpeg` in a loop to continuously feed video into it. When `ffmpeg` exits (or fails), the script automatically restarts it. `ffmpeg`'s normal behavior includes exiting when the FIFO's reader closes the FIFO. This happens whenever the camera's stream is stopped in the guest.

In both modes (Test Source and Video File), the script will print an argument to add to `cvd create`/`cvd start` that will provide a video device in the guest that matches the host configuration.

### 1. Test Source Mode (Default)

If no video file is provided, the script uses `ffmpeg`'s `testsrc` filter to generate a synthetic test pattern (color bars and a timer).

To start streaming a 640x480 video at 30 fps (defaults):

```bash
tools/testutils/ffmpeg_v4l2_stream_proxy.sh
```

You can customize the resolution and frame rate:

```bash
tools/testutils/ffmpeg_v4l2_stream_proxy.sh -w 1280 -H 720 -r 60
```

Available options:
*   `-w <width>`: Video width (default: 640)
*   `-H <height>`: Video height (default: 480)
*   `-r <fps>`: Video FPS (default: 30)
*   `-p <fifo_path>`: Path to the FIFO to create/use (default: `/tmp/v4l2_fifo`)

### 2. Video File Mode

To stream a video file (e.g., `.mp4`, `.mjpeg`, `.h264`) in a loop:

```bash
tools/testutils/ffmpeg_v4l2_stream_proxy.sh -f /path/to/video.mp4
```

In this mode, the script uses `ffprobe` to automatically detect the video's resolution and frame rate.

Available options:
*   `-f <video_file>`: Path to the host video file
*   `-p <fifo_path>`: Path to the FIFO to create/use (default: `/tmp/v4l2_fifo`)

## Connecting to Cuttlefish (CVD)

Once the streaming script is running, start Cuttlefish and configure it to use the stream. Use the parameters printed by the script (or the defaults) to configure the `--media` flag.

Example CVD launch command:

```bash
cvd create --media=v4l2_stream_proxy:input_path=/tmp/v4l2_fifo:input_width=640:input_height=480:input_fps=30
```

## Stopping the Stream

Press `Ctrl+C` in the terminal running the script. The script will catch the interrupt, kill the active `ffmpeg` process, and clean up the FIFO if it was created by the script.
