#!/usr/bin/env bash

# Copyright (C) 2025 The Android Open Source Project
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

# Set up a riscv64 Debian/Ubuntu host for building android-cuttlefish.
#
# On x86_64 and aarch64, prebuilt binaries exist for bazel, cargo-bazel,
# and other tools.  On riscv64 we must build some from source.
# This script handles everything automatically.

set -e

if [ "$(uname -m)" != "riscv64" ]; then
  echo "This script is intended for riscv64 hosts only." >&2
  exit 1
fi

echo "Installing riscv64 build dependencies for android-cuttlefish..."

sudo apt-get update

# General build tools
sudo apt-get install -y \
  build-essential \
  git \
  openjdk-21-jdk \
  pkg-config \
  python3 \
  zip \
  unzip \
  wget

# LLVM/Clang toolchain - used by Bazel via toolchains_llvm.
# libc++ is required because toolchains_llvm defaults to builtin-libc++
# and expects headers at /usr/lib/llvm-19/include/c++/v1/.
# libclang-rt provides compiler-rt builtins (libclang_rt.builtins.a)
# needed by the linker.
sudo apt-get install -y \
  clang-19 \
  lld-19 \
  libc++-19-dev \
  libc++abi-19-dev \
  libclang-rt-19-dev

# Debian's clang-rt package uses the legacy layout:
#   lib/clang/19/lib/linux/libclang_rt.builtins-riscv64.a
# but toolchains_llvm expects the per-target layout:
#   lib/clang/19/lib/riscv64-unknown-linux-gnu/libclang_rt.builtins.a
# Create a symlink to bridge the two if needed.
LEGACY_RT="/usr/lib/llvm-19/lib/clang/19/lib/linux/libclang_rt.builtins-riscv64.a"
TARGET_DIR="/usr/lib/llvm-19/lib/clang/19/lib/riscv64-unknown-linux-gnu"
TARGET_RT="${TARGET_DIR}/libclang_rt.builtins.a"
if [ -f "$LEGACY_RT" ] && [ ! -e "$TARGET_RT" ]; then
  echo "Creating clang-rt per-target symlink..."
  sudo mkdir -p "$TARGET_DIR"
  sudo ln -sf "$LEGACY_RT" "$TARGET_RT"
elif [ -e "$TARGET_RT" ]; then
  echo "clang-rt per-target layout already exists, skipping symlink."
else
  echo "WARNING: libclang-rt-19-dev installed but $LEGACY_RT not found." >&2
  echo "Your distro may use a different layout. You may need to manually create:" >&2
  echo "  $TARGET_RT -> <path to libclang_rt.builtins for riscv64>" >&2
fi

# Rust toolchain
sudo apt-get install -y rustc cargo

# Bazel: no official riscv64 binary; build from source if not installed.
BAZEL_VERSION=8.5.1
if command -v bazel &>/dev/null; then
  echo "Bazel already installed: $(bazel --version)"
else
  echo "Building Bazel ${BAZEL_VERSION} from source (this takes 30-60 minutes)..."
  tmpdir="$(mktemp -d)"
  trap "rm -rf $tmpdir" EXIT
  pushd "$tmpdir"
  wget -q "https://github.com/bazelbuild/bazel/releases/download/${BAZEL_VERSION}/bazel-${BAZEL_VERSION}-dist.zip"
  unzip -q "bazel-${BAZEL_VERSION}-dist.zip"
  env EXTRA_BAZEL_ARGS="--tool_java_runtime_version=local_jdk" bash ./compile.sh
  sudo cp output/bazel /usr/local/bin/bazel
  popd
  echo "Bazel ${BAZEL_VERSION} installed to /usr/local/bin/bazel"
fi

# cargo-bazel: no prebuilt riscv64 binary; build from source if not installed.
# rules_rust's crate_universe needs this at build time.  The build expects
# CARGO_BAZEL_GENERATOR_URL to point to the binary.
if ! command -v cargo-bazel &>/dev/null; then
  echo "Building cargo-bazel from source..."
  cargo install cargo-bazel
fi
echo ""
echo "riscv64 host setup complete."
echo ""
echo "Before building, set the cargo-bazel generator URL:"
echo "  export CARGO_BAZEL_GENERATOR_URL=\"file://\$(which cargo-bazel)\""

echo "All done!"
