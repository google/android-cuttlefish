# Cuttlefish auto vehicle HAL implementation

This folder contains the cuttlefish auto (cf_auto) vehicle HAL (VHAL)
implementation. The 'android.hardware.automotive.vehicle@V3-cf-service' target
is the VHAL binary. 'apex' folder defines the
[vendor APEX](https://source.android.com/docs/core/ota/vendor-apex) for VHAL.

## Architecture

cf_auto VHAL is based on [Grpc VHAL architecture](https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/main/automotive/vehicle/aidl/impl/current/README.md#grpc).
The cvd launcher will starts a VHAL proxy server running on the host machine
and the VHAL binary running on the client Android VM connects to the proxy
server using GRPC.

We support both Ethernet and Vsock as the communication channel.

### Ethernet as communication channel

By default Ethernet is used. We use a ethernet setup script
(device/google/cuttlefish/shared/auto/auto_ethernet/ethernet.rc)
running during AAOS (client) boot that sets up the Ethernet environment.

It creates a network namespace: 'auto_eth' and move 'eth1' (which is the default
Ethernet interface that connects to the host) to the network namespace. We use
the network namespace to hide 'eth1' from the Android network stack to prevent
Android network daemon configuring it after we manually configure the IP
address. We then manually assign IP address and routing rule.

The default Ethernet IP address for the host is '192.168.98.1'. The Ethernet IP
address for each cf_auto clients are 192.168.98.[instance_id + 2], starting
from 192.168.98.3, 192.168.98.4, etc. This is passed to AAOS using the boot
argument "ro.boot.auto_eth_guest_addr".

For each `cvd create` command, we start one VHAL proxy server instance. If multiple
cf_auto instances are started in one command via '--instance_nums', each instance
connects to a dedicated VHAL proxy server starting at the port: 9300 +
[instance_id - 1].

For example, to launch two cf_auto instances simutaneously (a.k.a. multi-tenancy),
we can run:

```
cvd create --num_instances=2
```

This starts two cf_auto instances and two independent VHAL proxy servers. The VHAL
client on the first instance has the IP address `192.168.98.3`, connecting to the
VHAL proxy server at `192.168.98.1:9300`. The VHAL client on the second instance
has the IP address `192.168.98.4`, connecting to the VHAL proxy server at
`192.168.98.1:9301`.

### Vsock as communication channel

If 'ethernet.rc' is not included in the build (by configuring
`ENABLE_AUTO_ETHERNET` to false in mk file), we fallback to using vsock as
the communication channel.

The VHAL proxy server implementation serves as both an Ethernet server and
as a vsock server. In fact, `vhal_proxy_server_cmd` always connects to the
server using vsock.

The way to manage VHAL clients and servers in multi-tenancy remains the same
regardless of the communication channel. The only difference is that ethernet
and vsock utilize different addressing schemas.

The VHAL proxy server address is at `vsock:[VMADDR_CID_HOST]:port`, where the
port allocation reuses the ethernet setup (i.e. 9300 + [instance_id - 1]).

We do not need to assign vsock address to each client instance.

## Debug

Similar to regular VHAL, you could use the following debug command on the
cf_auto instance:

```
dumpsys android.hardware.automotive.vehicle.IVehicle/default
```

Alternatively, you may also directly issue debug command to the VHAL proxy
server. You could use the binary `vhal_proxy_server_cmd` on the host which will
connects to the server. It takes the same argument as the VHAL debug command
with an additional optional argument port, which specifies the server port.
For example, you could use:

```
vhal_proxy_server_cmd --port 9300 --set PERF_VEHICLE_SPEED -f 1.234
```