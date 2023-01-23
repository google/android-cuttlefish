/*
 * Copyright (C) 2021 The Android Open Source Project
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
 */

//! `append_squashfs_overlay` generates a new squashfs image(dest) which contains an overlay image(overlay) on an squashfs image(src).
//! The tool ignores the existing overlay image in src, that is, the overlay image could be replaced with a new overlay image.
use std::fs::File;
use std::io::{copy, Error, ErrorKind, Read, Result, Seek, SeekFrom};
use std::path::{Path, PathBuf};

use clap::{builder::ValueParser, Arg, ArgAction, Command};

// https://dr-emann.github.io/squashfs/squashfs.html
const BYTES_USED_FIELD_POS: u64 = (32 * 5 + 16 * 6 + 64) / 8;
const SQUASHFS_MAGIC: u32 = 0x73717368;

// https://git.openwrt.org/?p=project/fstools.git;a=blob;f=libfstools/rootdisk.c;h=9f2317f14e8d8f12c71b30944138d7a6c877b406;hb=refs/heads/master#l125
// 64kb alignment
const ROOTDEV_OVERLAY_ALIGN: u64 = 64 * 1024;

fn align_size(size: u64, alignment: u64) -> u64 {
    assert!(
        alignment > 0 && (alignment & (alignment - 1) == 0),
        "alignment should be greater than 0 and a power of 2."
    );
    (size + (alignment - 1)) & !(alignment - 1)
}

fn merge_fs(src: &Path, overlay: &Path, dest: &Path, overwrite: bool) -> Result<()> {
    if dest.exists() && !overwrite {
        return Err(Error::new(
            ErrorKind::AlreadyExists,
            "The destination file already exists, add -w option to overwrite.",
        ));
    }
    let mut buffer = [0; 4];

    let mut src = File::open(src)?;

    src.read_exact(&mut buffer)?;
    let magic = u32::from_le_bytes(buffer);
    if magic != SQUASHFS_MAGIC {
        return Err(Error::new(ErrorKind::InvalidData, "The source image isn't a squashfs image."));
    }
    src.seek(SeekFrom::Start(BYTES_USED_FIELD_POS))?;
    let mut buffer = [0; 8];
    src.read_exact(&mut buffer)?;

    // https://git.openwrt.org/?p=project/fstools.git;a=blob;f=libfstools/rootdisk.c;h=9f2317f14e8d8f12c71b30944138d7a6c877b406;hb=refs/heads/master#l125
    // use little endian
    let bytes_used = u64::from_le_bytes(buffer);
    let mut dest = File::create(dest)?;
    let mut overlay = File::open(overlay)?;

    src.seek(SeekFrom::Start(0))?;
    let mut src_handle = src.take(align_size(bytes_used, ROOTDEV_OVERLAY_ALIGN));
    copy(&mut src_handle, &mut dest)?;
    copy(&mut overlay, &mut dest)?;
    Ok(())
}

fn clap_command() -> Command {
    Command::new("append_squashfs_overlay")
        .arg(Arg::new("src").value_parser(ValueParser::path_buf()).required(true))
        .arg(Arg::new("overlay").value_parser(ValueParser::path_buf()).required(true))
        .arg(Arg::new("dest").value_parser(ValueParser::path_buf()).required(true))
        .arg(
            Arg::new("overwrite")
                .short('w')
                .required(false)
                .action(ArgAction::SetTrue)
                .help("whether the tool overwrite dest or not"),
        )
}

fn main() -> Result<()> {
    let matches = clap_command().get_matches();

    let src = matches.get_one::<PathBuf>("src").unwrap().as_ref();
    let overlay = matches.get_one::<PathBuf>("overlay").unwrap().as_ref();
    let dest = matches.get_one::<PathBuf>("dest").unwrap().as_ref();
    let overwrite = matches.get_flag("overwrite");

    merge_fs(src, overlay, dest, overwrite)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn verify_args() {
        clap_command().debug_assert();
    }
}
