// Copyright 2024, The Android Open Source Project
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

// Copied from AOSP on 2024-04-18
// https://android.googlesource.com/platform/system/librustutils/+/refs/heads/main/inherited_fd.rs

//! Library for safely obtaining `OwnedFd` for inherited file descriptors.

use nix::fcntl::{fcntl, FdFlag, F_SETFD};
use nix::libc;
use std::collections::HashMap;
use std::fs::canonicalize;
use std::fs::read_dir;
use std::os::fd::FromRawFd;
use std::os::fd::OwnedFd;
use std::os::fd::RawFd;
use std::sync::Mutex;
use std::sync::OnceLock;
use thiserror::Error;

/// Errors that can occur while taking an ownership of `RawFd`
#[derive(Debug, PartialEq, Error)]
pub enum Error {
    /// init_once() not called
    #[error("init_once() not called")]
    NotInitialized,

    /// Ownership already taken
    #[error("Ownership of FD {0} is already taken")]
    OwnershipTaken(RawFd),

    /// Not an inherited file descriptor
    #[error("FD {0} is either invalid file descriptor or not an inherited one")]
    FileDescriptorNotInherited(RawFd),

    /// Failed to set CLOEXEC
    #[error("Failed to set CLOEXEC on FD {0}")]
    FailCloseOnExec(RawFd),
}

static INHERITED_FDS: OnceLock<Mutex<HashMap<RawFd, Option<OwnedFd>>>> = OnceLock::new();

/// Take ownership of all open file descriptors in this process, which later can be obtained by
/// calling `take_fd_ownership`.
///
/// # Safety
/// This function has to be called very early in the program before the ownership of any file
/// descriptors (except stdin/out/err) is taken.
pub unsafe fn init_once() -> Result<(), std::io::Error> {
    let mut fds = HashMap::new();

    let fd_path = canonicalize("/proc/self/fd")?;

    for entry in read_dir(&fd_path)? {
        let entry = entry?;

        // Files in /prod/self/fd are guaranteed to be numbers. So parsing is always successful.
        let file_name = entry.file_name();
        let raw_fd = file_name.to_str().unwrap().parse::<RawFd>().unwrap();

        // We don't take ownership of the stdio FDs as the Rust runtime owns them.
        if [libc::STDIN_FILENO, libc::STDOUT_FILENO, libc::STDERR_FILENO].contains(&raw_fd) {
            continue;
        }

        // Exceptional case: /proc/self/fd/* may be a dir fd created by read_dir just above. Since
        // the file descriptor is owned by read_dir (and thus closed by it), we shouldn't take
        // ownership to it.
        if entry.path().read_link()? == fd_path {
            continue;
        }

        // SAFETY: /proc/self/fd/* are file descriptors that are open. If `init_once()` was called
        // at the very beginning of the program execution (as requested by the safety requirement
        // of this function), this is the first time to claim the ownership of these file
        // descriptors.
        let owned_fd = unsafe { OwnedFd::from_raw_fd(raw_fd) };
        fds.insert(raw_fd, Some(owned_fd));
    }

    INHERITED_FDS
        .set(Mutex::new(fds))
        .or(Err(std::io::Error::other("Inherited fds were already initialized")))
}

/// Take the ownership of the given `RawFd` and returns `OwnedFd` for it. The returned FD is set
/// CLOEXEC. `Error` is returned when the ownership was already taken (by a prior call to this
/// function with the same `RawFd`) or `RawFd` is not an inherited file descriptor.
pub fn take_fd_ownership(raw_fd: RawFd) -> Result<OwnedFd, Error> {
    let mut fds = INHERITED_FDS.get().ok_or(Error::NotInitialized)?.lock().unwrap();

    if let Some(value) = fds.get_mut(&raw_fd) {
        if let Some(owned_fd) = value.take() {
            fcntl(raw_fd, F_SETFD(FdFlag::FD_CLOEXEC)).or(Err(Error::FailCloseOnExec(raw_fd)))?;
            Ok(owned_fd)
        } else {
            Err(Error::OwnershipTaken(raw_fd))
        }
    } else {
        Err(Error::FileDescriptorNotInherited(raw_fd))
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use anyhow::Result;
    use nix::fcntl::{fcntl, FdFlag, F_GETFD, F_SETFD};
    use nix::unistd::close;
    use std::os::fd::{AsRawFd, IntoRawFd};
    use tempfile::tempfile;

    struct Fixture {
        fds: Vec<RawFd>,
    }

    impl Fixture {
        fn setup(num_fds: usize) -> Result<Self> {
            let mut fds = Vec::new();
            for _ in 0..num_fds {
                fds.push(tempfile()?.into_raw_fd());
            }
            Ok(Fixture { fds })
        }

        fn open_new_file(&mut self) -> Result<RawFd> {
            let raw_fd = tempfile()?.into_raw_fd();
            self.fds.push(raw_fd);
            Ok(raw_fd)
        }
    }

    impl Drop for Fixture {
        fn drop(&mut self) {
            self.fds.iter().for_each(|fd| {
                let _ = close(*fd);
            });
        }
    }

    fn is_fd_opened(raw_fd: RawFd) -> bool {
        fcntl(raw_fd, F_GETFD).is_ok()
    }

    #[test]
    fn all_in_one() -> Result<()> {
        // Typically, all tests run in the same process in multiple threads, but init_once must be
        // run only once. By having a single test we ensure that init_once is only run at a time
        // predictable by all tests.
        let mut fixture = Fixture::setup(6)?;

        // access_without_init_once
        {
            let f0 = fixture.fds[0];
            assert_eq!(Some(Error::NotInitialized), take_fd_ownership(f0).err());
        }

        // SAFETY: assume files opened by Fixture are inherited ones
        unsafe {
            init_once()?;
        }

        // access_non_inherited_fd
        // this must run first to avoid it reusing a previously closed fd
        {
            let f_new = fixture.open_new_file()?;
            assert_eq!(Some(Error::FileDescriptorNotInherited(f_new)), take_fd_ownership(f_new).err());
        }

        // happy_case
        {
            let f1 = fixture.fds[1];
            let f2 = fixture.fds[2];
            let f1_owned = take_fd_ownership(f1)?;
            let f2_owned = take_fd_ownership(f2)?;
            assert_eq!(f1, f1_owned.as_raw_fd());
            assert_eq!(f2, f2_owned.as_raw_fd());

            drop(f1_owned);
            drop(f2_owned);
            assert!(!is_fd_opened(f1));
            assert!(!is_fd_opened(f2));
        }

        // double_ownership
        {
            let f = fixture.fds[3];

            let f_owned = take_fd_ownership(f)?;
            let f_double_owned = take_fd_ownership(f);
            assert_eq!(Some(Error::OwnershipTaken(f)), f_double_owned.err());

            // just to highlight that f_owned is kept alive when the second call to take_fd_ownership
            // is made.
            drop(f_owned);
        }

        // take_drop_retake
        {
            let f = fixture.fds[4];

            let f_owned = take_fd_ownership(f)?;
            drop(f_owned);

            let f_double_owned = take_fd_ownership(f);
            assert_eq!(Some(Error::OwnershipTaken(f)), f_double_owned.err());
        }

        // cloexec
        {
            let f = fixture.fds[5];

            // Intentionally cleaar cloexec to see if it is set by take_fd_ownership
            fcntl(f, F_SETFD(FdFlag::empty()))?;

            let f_owned = take_fd_ownership(f)?;
            let flags = fcntl(f_owned.as_raw_fd(), F_GETFD)?;
            assert_eq!(flags, FdFlag::FD_CLOEXEC.bits());
        }

        // call_init_once_multiple_times
        // this must be last
        {
            // SAFETY: for testing
            let res = unsafe { init_once() };
            assert!(res.is_err());
        }

        Ok(())
    }
}
