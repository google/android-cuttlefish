#!/bin/bash

# Copyright (C) 2026 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -e

# Defaults
WIDTH=640
HEIGHT=480
FPS=30
FIFO_PATH="/tmp/v4l2_fifo"
VIDEO_FILE=""

usage() {
  echo "Usage: $0 [-f <video_file>] [-w <width>] [-H <height>] [-r <fps>] [-p <fifo_path>] [-h]"
  echo "  -f <video_file>: Path to host video file to stream (e.g. MJPEG, MP4)"
  echo "  -w <width>: Video width for testsrc (default: 640)"
  echo "  -H <height>: Video height for testsrc (default: 480)"
  echo "  -r <fps>: Video FPS for testsrc (default: 30)"
  echo "  -p <fifo_path>: Path to FIFO (default: /tmp/v4l2_fifo)"
  echo "  -h: Show this help message"
  exit 0
}

while getopts "f:w:H:r:p:h" opt; do
  case $opt in
  f) VIDEO_FILE="$OPTARG" ;;
  w) WIDTH="$OPTARG" ;;
  H) HEIGHT="$OPTARG" ;;
  r) FPS="$OPTARG" ;;
  p) FIFO_PATH="$OPTARG" ;;
  h) usage ;;
  *) usage ;;
  esac
done

# Check dependencies
if ! command -v ffmpeg &>/dev/null; then
  echo "Error: ffmpeg is not installed!" >&2
  exit 1
fi

if [ -n "$VIDEO_FILE" ] && ! command -v ffprobe &>/dev/null; then
  echo "Error: ffprobe is not installed (required when -f is used)!" >&2
  exit 1
fi

FFMPEG_PID=""
CREATED_FIFO=false

cleanup() {
  echo "Stopping ffmpeg stream..."
  if [ -n "$FFMPEG_PID" ]; then
    echo "Killing ffmpeg (PID: $FFMPEG_PID)..."
    kill -9 $FFMPEG_PID 2>/dev/null || true
    wait $FFMPEG_PID 2>/dev/null || true
  fi
  if [ "$CREATED_FIFO" = true ] && [ -p "$FIFO_PATH" ]; then
    echo "Removing created FIFO: $FIFO_PATH"
    rm -f "$FIFO_PATH"
  fi
  exit 0
}
# We use EXIT trap to ensure cleanup runs when the script exits (normally or via signal)
# We also trap INT and TERM explicitly to trigger exit (which triggers EXIT trap)
trap "exit 0" INT TERM
trap cleanup EXIT

if [ ! -p "$FIFO_PATH" ]; then
  echo "Creating FIFO at $FIFO_PATH"
  mkfifo "$FIFO_PATH"
  CREATED_FIFO=true
fi

FFMPEG_ARGS=()

if [ -n "$VIDEO_FILE" ]; then
  if [ ! -f "$VIDEO_FILE" ]; then
    echo "Error: Video file $VIDEO_FILE not found!" >&2
    exit 1
  fi
  echo "Analyzing video file: $VIDEO_FILE..."
  WIDTH=$(ffprobe -v error -select_streams v:0 -show_entries stream=width -of default=nw=1:nk=1 "$VIDEO_FILE")
  HEIGHT=$(ffprobe -v error -select_streams v:0 -show_entries stream=height -of default=nw=1:nk=1 "$VIDEO_FILE")
  FPS_RATIO=$(ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate -of default=nw=1:nk=1 "$VIDEO_FILE")
  if [ "$FPS_RATIO" = "0/0" ] || [ -z "$FPS_RATIO" ]; then
    FPS_RATIO=$(ffprobe -v error -select_streams v:0 -show_entries stream=avg_frame_rate -of default=nw=1:nk=1 "$VIDEO_FILE")
  fi
  FPS="$FPS_RATIO"
  echo "Detected parameters: Width=$WIDTH, Height=$HEIGHT, FPS=$FPS"
  
  FFMPEG_ARGS=(-re -stream_loop -1 -i "$VIDEO_FILE")
  echo "Streaming from $VIDEO_FILE to $FIFO_PATH..."
else
  FFMPEG_ARGS=(-re -f lavfi -i "testsrc=size=${WIDTH}x${HEIGHT}:rate=${FPS}")
  echo "Streaming testsrc (${WIDTH}x${HEIGHT} @ ${FPS}fps) to $FIFO_PATH..."
fi

echo "Suggested CVD config: --media=v4l2_stream_proxy:input_path=$FIFO_PATH:input_width=$WIDTH:input_height=$HEIGHT:input_fps=$FPS"
echo "Streaming started. Press Ctrl+C to stop."

while true; do
  echo "Starting ffmpeg..."
  ffmpeg -y "${FFMPEG_ARGS[@]}" -f rawvideo -pix_fmt yuv420p "$FIFO_PATH" >/dev/null 2>&1 &
  FFMPEG_PID=$!
  wait $FFMPEG_PID || true
  FFMPEG_PID=""
done
