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

For each launch_cvd command, we start one VHAL proxy server instance by
default. If multiple cf_auto instances are started in one command via
'--instance_nums', then they all connect to the same server. The VHAL proxy
server starts at the port: 9300 + [base_instance_num - 1].

Clients may specify '--vhal_proxy_server_instance_num' to specify which server
to connect to. If this is specified, no VHAL proxy server will be started,
instead, the instances will connect to the VHAL proxy server started by
the 'vhal_proxy-server_instance_num'.

For example, if we want to start two instances that all connects to the same
VHAL proxy server (meaning they share VHAL data), we can use:

```
launch_cvd --instance_nums 2
```

This starts two cf_auto instances and starts a VHAL proxy server running at
`192.168.98.1:9300`. The two instaces will have IP address: `192.168.98.3`,
`192.168.98.4`. Their VHALs connect to the VHAL proxy server.

If we want to start a third instance that connects to the same VHAL proxy
server using a separate command, we can use:

```
launch_cvd --base_instance_num=3 --vhal_proxy_server_instance_num=1
```

This starts another cf_auto instance at `192.168.98.5` and connects to the
VHAL proxy server at `192.168.98.1:9300`.

If we want to start a fourth instance that connects to a new VHAL proxy server,
we can use:

```
launch_cvd --base_instance_num=4
```

This starts another cf_auto instance at `192.168.98.5` and starts a new VHAL
proxy server at `192.168.98.1:9303`. The new instance connects to this new
server.

These options apply for 'vsock' as communication as well, except that vsock
address takes a different format.

### Vsock as communication channel

If 'ethernet.rc' is not included in the build (by configuring
`ENABLE_AUTO_ETHERNET` to false in mk file), we fallback to using vsock as
the communication channel.

The VHAL proxy server implementation serves as both an Ethernet server and
as a vsock server. In fact, `vhal_proxy_server_cmd` always connects to the
server using vsock.

The concept is the same except that vsock uses a different address schema.

The VHAL proxy server address is at `vsock:[VMADDR_CID_HOST]:port`, where port
is the ethernet port (e.g. 9300).

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