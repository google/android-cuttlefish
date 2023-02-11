//! Test comms client, but in Rust

use binder::{StatusCode, Strong};
use com_android_minidroid_testservice::aidl::com::android::minidroid::testservice::ITestService::ITestService;
use com_android_minidroid_testservice::binder;
use rpcbinder::RpcSession;

fn get_service(cid: u32, port: u32) -> Result<Strong<dyn ITestService>, StatusCode> {
    RpcSession::new().setup_vsock_client(cid, port)
}

fn main() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    if std::env::args().len() != 3 {
        return Err(format!("usage: {} CID port", std::env::args().next().unwrap()).into());
    }

    let service_host_cid =
        std::env::args().nth(1).and_then(|arg| arg.parse::<u32>().ok()).expect("invalid CID");
    let service_port =
        std::env::args().nth(2).and_then(|arg| arg.parse::<u32>().ok()).expect("invalid port");

    println!(
        "Hello Rust Minidroid client! Connecting to CID {} and port {}",
        service_host_cid, service_port
    );

    let service = get_service(service_host_cid, service_port)?;
    service.sayHello()?;
    service.printText("Hello from Rust client! 🦀")?;
    let result = service.addInteger(4, 6)?;
    println!("Finished client. 4 + 6 = {}", result);

    Ok(())
}
