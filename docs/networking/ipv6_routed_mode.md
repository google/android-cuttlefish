# IPv6 routed mode (static networking mode)

By default `cuttlefish-host-resources` gives the guests private IPv6
addresses (Unique Local Addresses, `fd00:cf:2X::`) and NATs them (NAT66) when
they leave the host, as it does for IPv4. See
[openwrt_ipv6_static.md](openwrt_ipv6_static.md).

In **routed mode** the host instead gives every guest network its own `/64`
from a global `/48` that the upstream network routes to the host. There is no
NAT66: servers see each guest's own address, and the address differs per guest
network. This is what tests such as CTS
`ConnectivityManagerTest#testOpenConnection` check: the address an echo server
sees over Wi-Fi and over mobile data must differ, and must be on that
network's link. A NATed setup cannot pass that check.

Private mode stays the default. Routed mode only adds behavior.

## Requirements

- A global IPv6 **`/48`** routed to the Cuttlefish host by the upstream
  router (static route or DHCPv6 prefix delegation). Every guest network
  uses SLAAC or a fixed `/64`, so the host needs one `/64` per network and
  per instance; the layout below uses the fourth hextet for that.
- A prefix smaller than a `/48` does not fit the layout. In particular, a
  `/64` on the host's own uplink, or the `/96` that some cloud VMs get per
  network interface (for example
  [Compute Engine](https://cloud.google.com/compute/docs/ip-addresses/configure-ipv6-address)),
  cannot be split into `/64`s for SLAAC. Such a host can only use private
  mode.
- The init script promotes `accept_ra` to `2` on `default` and on existing
  non-Cuttlefish interfaces that use kernel router advertisements before
  enabling IPv6 forwarding (`net.ipv6.conf.all.forwarding=1`), so the host's
  uplink keeps its RA default route with forwarding on.

## Host configuration

In `/etc/default/cuttlefish-host-resources`:

```
ipv6_routed_prefix=2001:db8:cf00::/48
#ipv6_nat=0
```

- `ipv6_routed_prefix` must be written as `a:b:c::/48` (or `a:b::/48`,
  `a::/48`). Any other value (another length, bits set after the first 48)
  is rejected with a message on standard error and the host uses private
  mode.
- `ipv6_nat` defaults to `0` in routed mode and `1` in private mode. With
  `ipv6_nat=1` in routed mode, the host NATs the routed `/48` (source set by
  `ipv6_nat_source`, which defaults to the routed `/48`).
- In routed mode the `*_ipv6_prefix` and `*_ipv6_prefix_base` settings are
  ignored.

Restart the service after changing the file
(`sudo systemctl restart cuttlefish-host-resources`).

## Address layout

`P` is the first three hextets of the `/48`; `NN` is the instance number as
two hex digits (`01` to `80`).

| Network | Host interface | Prefix | Host | Guest |
| --- | --- | --- | --- | --- |
| Ethernet | `cvd-ebr` | `P:24::/64` | `P:24::1` | SLAAC (RA from the host) |
| Legacy Wi-Fi bridge | `cvd-wbr` | `P:22::/64` | `P:22::1` | SLAAC (RA from the host) |
| Mobile | `cvd-mtap-NN` | `P:21NN::/64` | `::1` | `::2` from `modem_simulator` |
| OpenWrt WAN | `cvd-wifiap-NN` | `P:23NN::/64` | `::1` | OpenWrt WAN `::2` (static) |
| OpenWrt Wi-Fi LAN | behind OpenWrt | `P:25NN::/64` | route via `P:23NN::2` | SLAAC (RA from OpenWrt) |

Example for `2001:db8:cf00::/48` and instance 1: the guest mobile address is
`2001:db8:cf00:2101::2`, and the guest `wlan0` gets a SLAAC address in
`2001:db8:cf00:2501::/64`.

The host:

- assigns the host addresses above and sends router advertisements on
  `cvd-ebr`, `cvd-wbr` and every `cvd-wifiap-NN`, as in private mode;
- adds `ip -6 route add P:25NN::/64 via P:23NN::2 dev cvd-wifiap-NN` for every
  instance and records the routes in `/run/cuttlefish/ipv6-routes`, so `stop`
  (and a repeated `start`) removes them even if the configuration changed;
- keeps the router advertisement guard (guests cannot send RAs or redirects)
  and explicit `iifname`/`oifname "cvd-*"` accept rules in `ip6 filter FORWARD`;
- adds no NAT66 rule (the `ip6 cuttlefish_nat6` table exists, but is empty).

## How the device learns routed mode

No new `cvd`/`launch_cvd` flag is needed. The host tools read the host
address of each instance's taps:

- `assemble_cvd` (`network_flags.cpp`) reads the first global IPv6 address of
  `cvd-mtap-NN` and gives the guest the next address (`::2`), gateway `::1`.
  This is the same code as in private mode.
- `OpenwrtArgsFromConfig()` (`openwrt_args.cpp`) reads the address of
  `cvd-wifiap-NN`. Only when it is a global (non-ULA) `P:23NN::1/64` does it
  add these OpenWrt kernel command line arguments:

  | Key | Value |
  | --- | --- |
  | `wan_ip6addr` | `P:23NN::2/64` |
  | `wan_ip6gw` | `P:23NN::1` |
  | `lan_ip6prefix` | `P:25NN::/64` |

  With the private mode address (`fd00:cf:23:<i>::1`), nothing is added and
  OpenWrt keeps its default configuration. The bridged Wi-Fi tap
  (`cvd-wbr`) and cvdalloc modes do not get these arguments.

The OpenWrt image (`external/openwrt-prebuilts`,
`shared/uci-defaults/0_default_config`) reads these keys from
`/proc/cmdline`: when `lan_ip6prefix` is present it sets the static WAN IPv6
address and gateway, sets that `/64` as the LAN prefix, and disables `masq6`
on the `wan` zone. Otherwise it keeps the ULA LAN prefix and `masq6`. An
OpenWrt image without this support keeps NAT66 on the Wi-Fi path, so the
Wi-Fi address that servers see is the OpenWrt WAN address, not the guest's.

## Tests

- `e2etests/host_resources/static_resources_init_test` (`routed_test.go`)
  runs the init script in a rootless network namespace sandbox: address
  layout, router advertisement prefixes, no NAT66 rule, routes to the OpenWrt
  LAN `/64`s, cleanup on `stop` and on a repeated `start`, the `ipv6_nat`
  override, and the fallback to private mode for invalid prefixes.
- `e2etests/cvd/networking_tests` (`routed_echo_test.go`,
  `TestIPv6RoutedEcho`) fetches an echo URL from the guest over Wi-Fi and over
  mobile data separately (`curl --interface`, as root on a debuggable image)
  and checks that the address the server saw differs per network and is one
  of the guest's own addresses on that interface. It is skipped unless
  `--routed_echo_url` is given:

  ```
  bazel test //cvd/networking_tests:networking_tests \
    --test_arg=--routed_echo_url=http://[2001:db8:ec00::80]/ \
    --test_arg=-test.run=TestIPv6RoutedEcho
  ```

  The server must answer with the caller's address in the response body.
  Use an IPv6 literal URL, so the request cannot use IPv4.
- The unit tests `//cuttlefish/host/libs/config:openwrt_args_test` and
  `//cuttlefish/host/commands/assemble_cvd:network_flags_test` cover the
  address derivation.
