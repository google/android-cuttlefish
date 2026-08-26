//! vhost-user input device

mod buf_reader;
mod event_source;
mod inherited_fd;
mod vhu_input;
mod vio_input;

use std::fs;
use std::os::fd::{AsFd, AsRawFd, FromRawFd, IntoRawFd};
use std::str::FromStr;
use std::sync::{Arc, Mutex};

use anyhow::{anyhow, bail, Context, Result};
use clap::Parser;
use log::{error, info, LevelFilter};
use vhost::vhost_user::Listener;
use vhost_user_backend::VhostUserDaemon;
use vm_memory::{GuestMemoryAtomic, GuestMemoryMmap};

use event_source::PipeEventSource;
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

    // SAFETY: No choice but to trust the caller passed a valid fd representing a unix socket.
    let server_fd = inherited_fd::take_fd_ownership(args.socket_fd)
        .context("Failed to take ownership of socket fd")?;
    loop {
        let event_source = PipeEventSource::new(std::io::stdin(), std::io::stdout());
        let event_source_fd = event_source.as_fd().as_raw_fd();
        let backend = Arc::new(Mutex::new(VhostUserInput::new(
            device_config.clone(),
            event_source,
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
            // vhost::vhost_user::Listener takes ownership of the underlying fd and closes it when
            // wait returns, so a dup of the original fd is passed to the constructor.
            let server_dup = server_fd.try_clone().context("Failed to clone socket fd")?;
            // SAFETY: Safe because we just dupped this fd and don't use it anywhwere else.
            // Listener takes ownership and ensures it's properly closed when finished with it.
            unsafe { Listener::from_raw_fd(server_dup.into_raw_fd()) }
        };
        info!("Created vhost-user daemon");
        daemon
            .start(listener)
            .map_err(|e| anyhow!("Failed to start vhost-user daemon: {:?}", e))?;
        info!("Accepted connection in vhost-user daemon");
        if let Err(e) = daemon.wait() {
            // This will print an error even when the frontend disconnects to do a restart.
            error!("Error: {:?}", e);
        };
        info!("Daemon exited");
    }
}
