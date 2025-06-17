#!/bin/bash
#
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script is used to generate files in the <platform> directories needed to
# build libvpx. Every time libvpx source code is updated run this script.
#
# The script depends on the bpfmt tool, which may need to be built with
# m -j blueprint_tools
#
# For example, from the top of an Android tree:
# $ source build/envsetup.sh
# $ m -j blueprint_tools
# $ external/libvpx/post_update.sh
#
# And this will update all the config files needed.

set -e

if [[ "$#" != 1 ]]; then
  echo "Missing libvpx source directory"; exit 1;
fi

export LC_ALL=C
BIN_DIR=$(realpath $(dirname $0))
BASE_DIR=$(pwd)
LIBVPX_SRC_DIR=$(realpath "$1")
LIBVPX_CONFIG_DIR="$BASE_DIR/config"

# Clean files from previous make.
function make_clean {
  make clean > /dev/null
  rm -f libvpx_srcs.txt
}

# Lint a pair of vpx_config.h and vpx_config.asm to make sure they match.
# $1 - Header file directory.
function lint_config {
  $BIN_DIR/lint_config.sh \
    -h $LIBVPX_CONFIG_DIR/$1/vpx_config.h \
    -a $LIBVPX_CONFIG_DIR/$1/vpx_config.asm
}

# Generate *_rtcd.h files.
# $1 - Header file directory.
# $2 - Architecture.
# $3 - Optional - any additional arguments to pass through.
function gen_rtcd_header {
  echo "Generate $LIBVPX_CONFIG_DIR/$1/*_rtcd.h files."

  rm -rf $TEMP_DIR/libvpx.config
  $BIN_DIR/lint_config.sh -p \
    -h $LIBVPX_CONFIG_DIR/$1/vpx_config.h \
    -a $LIBVPX_CONFIG_DIR/$1/vpx_config.asm \
    -o $TEMP_DIR/libvpx.config

  $LIBVPX_SRC_DIR/build/make/rtcd.pl \
    --arch=$2 \
    --sym=vp8_rtcd $3 \
    --config=$TEMP_DIR/libvpx.config \
    $LIBVPX_SRC_DIR/vp8/common/rtcd_defs.pl \
    > $LIBVPX_CONFIG_DIR/$1/vp8_rtcd.h

  $LIBVPX_SRC_DIR/build/make/rtcd.pl \
    --arch=$2 \
    --sym=vp9_rtcd $3 \
    --config=$TEMP_DIR/libvpx.config \
    $LIBVPX_SRC_DIR/vp9/common/vp9_rtcd_defs.pl \
    > $LIBVPX_CONFIG_DIR/$1/vp9_rtcd.h

  $LIBVPX_SRC_DIR/build/make/rtcd.pl \
    --arch=$2 \
    --sym=vpx_scale_rtcd $3 \
    --config=$TEMP_DIR/libvpx.config \
    $LIBVPX_SRC_DIR/vpx_scale/vpx_scale_rtcd.pl \
    > $LIBVPX_CONFIG_DIR/$1/vpx_scale_rtcd.h

  $LIBVPX_SRC_DIR/build/make/rtcd.pl \
    --arch=$2 \
    --sym=vpx_dsp_rtcd $3 \
    --config=$TEMP_DIR/libvpx.config \
    $LIBVPX_SRC_DIR/vpx_dsp/vpx_dsp_rtcd_defs.pl \
    > $LIBVPX_CONFIG_DIR/$1/vpx_dsp_rtcd.h

  rm -rf $TEMP_DIR/libvpx.config
}

# Generate Config files. "--enable-external-build" must be set to skip
# detection of capabilities on specific targets.
# $1 - Header file directory.
# $2 - Config command line.
function gen_config_files {
  ./configure $2 > /dev/null

  # Generate vpx_config.asm for x86[_64].
  if [[ "$1" == *x86* ]]; then
    egrep "#define [A-Z0-9_]+ [01]" vpx_config.h \
      | awk '{print "%define " $2 " " $3}' > vpx_config.asm
  else
    # vpx_config.asm is unused for arm[64] but is needed to pass lint_config.
    egrep "#define [A-Z0-9_]+ [01]" vpx_config.h \
      | awk '{print $2 " EQU " $3}' \
      | perl $LIBVPX_SRC_DIR/build/make/ads2gas.pl > vpx_config.asm
  fi

  # Generate vpx_version.h
  $LIBVPX_SRC_DIR/build/make/version.sh "$LIBVPX_SRC_DIR" vpx_version.h

  cp vpx_config.* vpx_version.h $LIBVPX_CONFIG_DIR/$1
  make_clean
  rm -rf vpx_config.* vpx_version.h
}

echo "Create temporary directory."
TEMP_DIR="$BASE_DIR/libvpx.temp"
rm -rf $TEMP_DIR
cp -R $LIBVPX_SRC_DIR $TEMP_DIR
cd $TEMP_DIR

for arch in x86 x86_64 arm-neon arm64 generic; do
  mkdir -p "$LIBVPX_CONFIG_DIR/$arch";
done

echo "Generate config files."
all_platforms="--enable-external-build --enable-realtime-only --enable-pic"
all_platforms+=" --disable-install-docs"
all_platforms+=" --size-limit=4096x3072 --enable-vp9-highbitdepth"
gen_config_files x86 "--target=x86-linux-gcc ${all_platforms}"
gen_config_files x86_64 "--target=x86_64-linux-gcc ${all_platforms}"
gen_config_files arm-neon \
  "--target=armv7-linux-gcc ${all_platforms} --disable-runtime-cpu-detect"
gen_config_files arm64 "--target=armv8-linux-gcc ${all_platforms}"
gen_config_files generic "--target=generic-gnu ${all_platforms}"

echo "Remove temporary directory."
cd $BASE_DIR
rm -rf $TEMP_DIR

echo "Create temporary directory."
TEMP_DIR="$BASE_DIR/libvpx.temp"
rm -rf $TEMP_DIR
cp -R $LIBVPX_SRC_DIR $TEMP_DIR
cd $TEMP_DIR

gen_rtcd_header x86 x86
gen_rtcd_header x86_64 x86_64
gen_rtcd_header arm-neon armv7
gen_rtcd_header arm64 armv8
gen_rtcd_header generic generic

