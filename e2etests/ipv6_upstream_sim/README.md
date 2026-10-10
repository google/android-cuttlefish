# IPv6 upstream simulator for Cuttlefish routed mode

A small Docker image that plays the part of an ISP or lab router for a
Cuttlefish host running in IPv6 **routed mode**
(`ipv6_routed_prefix=P::/48` in `/etc/default/cuttlefish-host-resources`).
It lets you test routed mode, and the CTS test
`ConnectivityManagerTest#testOpenConnection`, on a machine that has no routed
IPv6 prefix.

The container provides:

| Role | Detail |
|---|---|
| IPv6 router | Routes `P::/48` to the Cuttlefish host over a transit link. No NAT. |
| Echo server | HTTP (80) and HTTPS (443). Any `GET` returns the client source address as plain text with no trailing newline, the same format as `https://google-ipv6test.appspot.com/ip.js?fmt=text`. |
| DNS server | `dnsmasq`. Answers AAAA for configured names with the echo server address. Other names are refused (no upstream resolver). |
| Lab CA | `gen_certs.sh` creates a CA and a server certificate for the configured names. The CA is also written in Android trust-store format (`<subject_hash_old>.0`). |

Files: [`Dockerfile`](Dockerfile), [`entrypoint.sh`](entrypoint.sh),
[`echo_server.py`](echo_server.py), [`gen_certs.sh`](gen_certs.sh),
[`sim_up.sh`](sim_up.sh), [`sim_down.sh`](sim_down.sh),
[`selftest.sh`](selftest.sh).

## Topology

Defaults (all addresses are from the documentation range `2001:db8::/32`,
[RFC 3849](https://www.rfc-editor.org/rfc/rfc3849)):

```
  simulator container (--network none)          Cuttlefish host netns (TARGET)
 +----------------------------------+          +-------------------------------------+
 | inet0 (dummy)                    |          |                                     |
 |   2001:db8:eeee::80  echo        |  veth    |  cf-upstream0  2001:db8:ffff::2/64  |
 |   2001:db8:eeee::53  DNS         |  pair    |    route ::/0 via 2001:db8:ffff::1  |
 | transit0  2001:db8:ffff::1/64  <-+----------+->                                   |
 |   route P::/48 via ffff::2       |          |  cvd-mtap-NN   P:21NN::1/64 -> guest P:21NN::2
 +----------------------------------+          |  cvd-wbr       P:22::1/64   -> Wi-Fi guests
                                               |  cvd-wifiap-NN P:23NN::1/64 -> OpenWrt WAN P:23NN::2
                                               |  cvd-ebr       P:24::1/64                   |
                                               |  route P:25NN::/64 via P:23NN::2 (OpenWrt LAN)
                                               +-------------------------------------+
  P = 2001:db8:cf00 by default (--prefix)
```

Traffic from a guest address such as `P:2101::2` leaves the Cuttlefish host
unchanged (routed mode has no NAT66), reaches the echo server, and the echo
server returns `P:2101::2`. That is what the IPv6 on-link check in
`testOpenConnection` requires.

## Usage

```bash
# 1. Start and attach to a Cuttlefish host network namespace.
./sim_up.sh <TARGET>
#    TARGET = PID of a process in the namespace, a netns file
#             (/run/netns/NAME, /proc/PID/ns/net), or "host" (+ --allow-host).
#    Options: --prefix P::/48  --transit T::/64  --route default|inet
#             --names "a b c"  --state DIR  --no-build   (see --help)

# 2. Stop. Deleting the container removes the veth pair and the route.
./sim_down.sh [--purge-certs]

# Self-test (no Cuttlefish; uses a throwaway rootless netns).
./selftest.sh
```

`sim_up.sh`:

1. Builds the image (`cf-ipv6-upstream-sim`).
2. Generates the lab CA and server certificate into `--state`
   (default `~/.cache/cf-ipv6-upstream-sim`), as the calling user.
3. Starts the container with `--network none --cap-add NET_ADMIN` and
   `--sysctl net.ipv6.conf.all.forwarding=1`.
4. Runs a short-lived privileged helper container
   (`--privileged --pid=host --network none`) that creates the veth pair
   between the container namespace and TARGET, assigns `T::1` / `T::2`, adds
   `P::/48 via T::2` in the container and `::/0 via T::1` (or only the
   simulator prefix with `--route inet`) in TARGET, and pings `T::1`.
5. Prints the status.

The helper is needed because a veth pair that spans two namespaces must be
created by a process with `CAP_NET_ADMIN` over both. It changes nothing
outside the two namespaces, except with `TARGET=host`.

### Connecting to a Cuttlefish host

- **Cuttlefish host in a network namespace (recommended).** Run the host
  init script and `cvd` inside a namespace (for example `unshare --user --net
  --mount ...`), then `./sim_up.sh <PID of a process in it>`. The real host
  network is untouched.
- **Cuttlefish on the real host.** `./sim_up.sh --allow-host host`. This adds
  `cf-upstream0` and an IPv6 default route to the host. Use `--route inet` to
  add only a route to `2001:db8:eeee::/64` and keep the existing default
  route. With `--route inet`, guest traffic reaches only the simulator.
- **Other wiring.** Any link works if the two routes exist: `P::/48` via the
  Cuttlefish host in the simulator, and the simulator addresses via the
  simulator in the Cuttlefish host. For example, a Docker network with
  `--ipv6` plus `ip -6 route add P::/48 via <host address>` inside the
  container, or macvlan on a lab interface.

The Cuttlefish host must forward IPv6 (`net.ipv6.conf.all.forwarding=1`);
the Cuttlefish host init script already sets this.

### DNS for guests

The simulator does not change guest DNS. To make guests use it, point the
resolver the guests use at `2001:db8:eeee::53`, for example with a `server=`
line in the host's dnsmasq for the Cuttlefish bridges, or with the guest
network settings.

## Intercepted CTS runs (lab only)

Official CTS accepts only two echo URLs
([ConnectivityManagerTest.java](https://cs.android.com/android/platform/superproject/main/+/main:packages/modules/Connectivity/tests/cts/net/src/android/net/cts/ConnectivityManagerTest.java),
`ALLOWED_IP_ADDRESS_ECHO_URLS`):
`https://google-ipv6test.appspot.com/ip.js?fmt=text` and
`https://ipv6test.googleapis-cn.com/ip.js?fmt=text`. Both need the public
internet and a routed global prefix.

To run `testOpenConnection` against this simulator (verified on an Android 17
userdebug image, `aosp_cf_x86_64_only_phone`):

1. **Split DNS.** The CTS preparer still needs real names
   (`connectivitycheck.gstatic.com`, `www.google.com`), so forward only the
   echo names to the simulator. For example, a host `dnsmasq` that the guest
   networks use as resolver:
   `--server=/google-ipv6test.appspot.com/2001:db8:eeee::53
   --server=/ipv6test.googleapis-cn.com/2001:db8:eeee::53 --server=8.8.8.8`.
   Do not bind it to UDP port 5353 on the host: `adb` uses that port for mDNS.
2. **OpenWrt rebind protection.** OpenWrt's `dnsmasq` treats answers in
   `2001:db8::/32` as a DNS rebind attack and drops them (`possible DNS-rebind
   attack detected` in the OpenWrt console log), so Wi-Fi clients never see
   the AAAA record. With the default documentation prefix, turn it off in
   OpenWrt for the run:
   `uci set dhcp.@dnsmasq[0].rebind_protection=0; uci commit dhcp;
   /etc/init.d/dnsmasq restart`. A real global prefix (`--prefix`) is not
   affected.
3. **Lab CA.** On Android 14 and later the system CA store is
   `/apex/com.android.conscrypt/cacerts`, which is read-only. Copy the store
   to a writable directory, add `<hash>.0`, and bind-mount the copy over the
   APEX and system paths, in the mount namespace of every process that
   validates certificates (init, zygote, system_server, the network stack).
   The following works on a userdebug build after `adb root`:
   ```bash
   adb push ~/.cache/cf-ipv6-upstream-sim/<hash>.0 /data/local/tmp/
   adb shell 'mkdir -p /data/local/tmp/cacerts &&
     cp -a /apex/com.android.conscrypt/cacerts/* /data/local/tmp/cacerts/ &&
     cp /data/local/tmp/<hash>.0 /data/local/tmp/cacerts/ &&
     chmod 644 /data/local/tmp/cacerts/<hash>.0 &&
     chcon u:object_r:system_security_cacerts_file:s0 /data/local/tmp/cacerts /data/local/tmp/cacerts/* &&
     for pid in 1 $(pidof zygote zygote64 system_server com.android.networkstack); do
       nsenter --mount=/proc/$pid/ns/mnt -- mount --bind /data/local/tmp/cacerts /apex/com.android.conscrypt/cacerts
       nsenter --mount=/proc/$pid/ns/mnt -- mount --bind /data/local/tmp/cacerts /system/etc/security/cacerts
     done'
   ```
   The mounts do not survive a reboot.
4. **Certificate transparency (Android 17 and later).** Conscrypt enforces
   certificate transparency by default (compat change `407952621`,
   `DEFAULT_ENABLE_CERTIFICATE_TRANSPARENCY`). The lab certificate has no
   signed certificate timestamps, so the handshake fails with a misleading
   `Trust anchor for certification path not found` even though the CA is
   trusted. Disable the check for the test package only:
   `adb shell am compat disable --no-kill 407952621 android.net.cts`. The
   setting survives the APK reinstall done by the CTS runner.

**Label every result from such a run "lab, intercepted". It is not an
official CTS result** and cannot be used as CTS evidence for a device. It
only shows that the network setup satisfies the test logic.

### Certificate-transparency risk

Android can enforce certificate transparency (CT) for publicly trusted
certificates ([Android CT
docs](https://developer.android.com/privacy-and-security/security-config#CertificateTransparency)).
The lab CA is a private root and its certificates are not in any CT log. On
Android 17 the check applies to the lab CA as well (see step 4 above); on
older releases it did not get in the way. If it fails, the failure is a
limitation of the intercepted setup, not of routed mode.

Never use the lab CA key outside the lab. Anyone with `ca.key` can
impersonate any site to a device that trusts the CA. `sim_down.sh
--purge-certs` deletes it.

## Self-test

`selftest.sh` checks the simulator without Cuttlefish. It creates a rootless
network namespace (`unshare --user --map-root-user --net`) as the "Cuttlefish
host" (IPv6 forwarding on, `P:22::2/64` on a dummy interface) and a second
namespace as a guest behind veth `cvd-mtap-01` (host `P:2101::1/64`, guest
`P:2101::2/64`). It attaches the simulator and checks:

- AAAA answers for `google-ipv6test.appspot.com` and `echo.sim.test`, and an
  empty `NOERROR` answer for A.
- HTTP and HTTPS (with the lab CA) return the exact source address, for the
  forwarded guest (`P:2101::2`) and for the host-local address (`P:22::2`).
  No NAT, and different per network.
- HTTPS without the lab CA fails.

Container images do not have a working resolver (`--network none`), so the
echo server skips the `getfqdn()` lookup that Python's `HTTPServer` does at
bind time.
