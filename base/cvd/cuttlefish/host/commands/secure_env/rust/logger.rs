//
// Copyright (C) 2022 The Android Open Source Project
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

use std::ffi::CString;
use std::os::unix::ffi::OsStrExt;

/// Logger implementation that forwards logs to the Android C++ backend.
/// TODO(schuffelen): Use android_logger when rust works with host glibc, see aosp/1415969
pub struct AndroidCppLogger;

impl log::Log for AndroidCppLogger {
    fn enabled(&self, _metadata: &log::Metadata) -> bool {
        // Filtering is done in the underlying C++ logger, so indicate to the Rust code that all
        // logs should be included
        true
    }

    fn log(&self, record: &log::Record) {
        let file = record.file().unwrap_or("(no file)");
        let file_basename =
            std::path::Path::new(file).file_name().unwrap_or(std::ffi::OsStr::new("(no file)"));
        let file = CString::new(file_basename.as_bytes())
            .unwrap_or_else(|_| CString::new("(invalid file)").unwrap());
        let line = record.line().unwrap_or(0);
        let severity = match record.level() {
            log::Level::Trace => 0,
            log::Level::Debug => 1,
            log::Level::Info => 2,
            log::Level::Warn => 3,
            log::Level::Error => 4,
        };
        let tag = CString::new("secure_env::".to_owned() + record.target())
            .unwrap_or_else(|_| CString::new("(invalid tag)").unwrap());
        let msg = CString::new(format!("{}", record.args()))
            .unwrap_or_else(|_| CString::new("(invalid msg)").unwrap());
        // SAFETY: All pointer arguments are generated from valid owned CString instances.
        unsafe {
            secure_env_ffi::secure_env_log(
                file.as_ptr(),
                line,
                severity,
                tag.as_ptr(),
                msg.as_ptr(),
            );
        }
    }

    fn flush(&self) {}
}
