//! vhost-user input device

mod buf_reader;
mod event_source;
mod inherited_fd;
mod vhu_input;
mod vio_input;

use std::fs;
use std::io::ErrorKind;
use std::os::fd::{AsRawFd, FromRawFd, IntoRawFd, OwnedFd};
use std::os::unix::net::UnixListener;
use std::str::FromStr;
use std::sync::{Arc, Mutex};

use anyhow::{anyhow, bail, Context, Result};
use clap::Parser;
use log::{error, info, LevelFilter};
use vhost::vhost_user::{Error as VError, Listener};
use vhost_user_backend::{Error as VHUError, VhostUserDaemon};
use vm_memory::{GuestMemoryAtomic, GuestMemoryMmap};

use event_source::{EventSource, StdioEventSource, UnixSocketEventSource};
use vhu_input::{VhostUserInput, EVENTS_AVAILABLE};
use vio_input::VirtioInputConfig;

/// Vhost-user input server.
#[derive(Parser, Debug)]
#[command(about = None, long_about = None)]
struct Args {
    /// Log verbosity, one of Off, Error, Warning, Info, Debug, Trace.
    #[arg(short, long, default_value_t = String::from("Debug") )]
    verbosity: String,
    /// File descriptor for the vhost user backend unix socket.
    #[arg(short, long, required = true)]
    socket_fd: i32,
    /// Path to a file specifying the device's config in JSON format.
    #[arg(short, long, required = true)]
    device_config: String,
    /// File descriptor for the unix socket event sources will connect to.
    #[arg(long, default_value_t = -1i32)]
    server_fd: i32,
    /// Path to the event capture unix socket.
    #[arg(long, default_value_t = String::from(""))]
    capture_server_path: String,
}

fn init_logging(verbosity: &str) -> Result<()> {
    env_logger::builder()
        .format_timestamp_secs()
        .filter_level(
            LevelFilter::from_str(verbosity)
                .with_context(|| format!("Invalid log level: {}", verbosity))?,
        )
        .init();
    Ok(())
}

fn create_and_run_device<T: EventSource + 'static>(
    event_source: T,
    device_config: VirtioInputConfig,
    server_fd: OwnedFd,
) -> Result<()> {
    loop {
        let event_source_clone = event_source
            .try_clone()
            .context("Failed to clone event source")?;
        let event_source_fd = event_source_clone.as_fd().as_raw_fd();
        // vhost::vhost_user::Listener and UnixListener take ownership of the underlying fds and
        // close them when dropped, so dups of the original fds are used in each iteration.
        let server_dup = server_fd.try_clone().context("Failed to clone socket fd")?;
        let backend = Arc::new(Mutex::new(VhostUserInput::new(
            device_config.clone(),
            event_source_clone,
        )));
        let mut daemon = VhostUserDaemon::new(
            "vhost-user-input".to_string(),
            backend.clone(),
            GuestMemoryAtomic::new(GuestMemoryMmap::new()),
        )
        .map_err(|e| anyhow!("Failed to create vhost user daemon: {:?}", e))?;

        // Ideally, this registration would be done by the backend since it is the one that knows to
        // read the event source when the EVENTS_AVAILABLE event is generated. This is not possible
        // because register_listener attempts to lock the backend to call num_queues() which causes
        // a deadlock if the backend lock is already held.
        daemon
            .get_epoll_handlers()
            .first()
            .context("Daemon created without epoll handler threads")?
            .register_listener(
                event_source_fd,
                vmm_sys_util::epoll::EventSet::IN,
                EVENTS_AVAILABLE as u64,
            )
            .context("Failed to register epoll handler")?;

        let listener = {
            // SAFETY: Safe because we just dupped this fd and don't use it anywhwere else.
            // Listener takes ownership and ensures it's properly closed when finished with it.
            // TODO: Use safe Listener::from<UnixStream> after updating to a version that implements it
            unsafe { Listener::from_raw_fd(server_dup.into_raw_fd()) }
        };
        info!("Created vhost-user daemon");
        daemon
            .start(listener)
            .map_err(|e| anyhow!("Failed to start vhost-user daemon: {:?}", e))?;
        info!("Accepted connection in vhost-user daemon");
        match daemon.wait() {
            Err(VHUError::HandleRequest(VError::Disconnected)) => {
                info!("Frontend disconnected");
            }
            Err(e) => {
                bail!("Daemon exited with error: {:?}", e);
            }
            Ok(()) => {
                info!("Daemon exited");
            }
        }
    }
}

fn main() -> Result<()> {
    // SAFETY: First thing after main
    unsafe {
        inherited_fd::init_once()
            .context("Failed to take ownership of process' file descriptors")?
    };
    let args = Args::parse();
    init_logging(&args.verbosity)?;

    if args.socket_fd < 0 {
        bail!("Invalid socket file descriptor: {}", args.socket_fd);
    }

    let device_config_str =
        fs::read_to_string(args.device_config).context("Unable to read device config file")?;

    let device_config = VirtioInputConfig::from_json(device_config_str.as_str())
        .context("Unable to parse config file")?;

    let socket_fd = inherited_fd::take_fd_ownership(args.socket_fd)
        .context("Failed to take ownership of socket fd")?;
    if args.server_fd >= 0 {
        let server_fd = inherited_fd::take_fd_ownership(args.server_fd)
            .context("Failed to take ownership of socket fd")?;
        let event_source = if args.capture_server_path.is_empty() {
            UnixSocketEventSource::new(UnixListener::from(server_fd))?
        } else {
            match fs::remove_file(&args.capture_server_path) {
                Err(e) if e.kind() != ErrorKind::NotFound => {
                    error!("Failed to remove existing capture socket: {:?}", e);
                }
                _ => {}
            }
            UnixSocketEventSource::with_capture_server(
                UnixListener::from(server_fd),
                UnixListener::bind(&args.capture_server_path)
                    .context("Failed to create capture server unix socket")?,
            )?
        };
        create_and_run_device(event_source, device_config, socket_fd)?;
    } else {
        if !args.capture_server_path.is_empty() {
            bail!("--capture_server_path is only supported with --server_fd");
        }
        let event_source = StdioEventSource::new();
        create_and_run_device(event_source, device_config, socket_fd)?;
    };
    Ok(())
}
