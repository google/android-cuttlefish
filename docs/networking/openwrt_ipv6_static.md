# OpenWrt IPv6 in static networking mode

Status: companion AOSP change to
[`platform/external/openwrt-prebuilts`](https://android.googlesource.com/platform/external/openwrt-prebuilts/+/main/shared/config/).
Bug: b/549899406.

## Topology

With `virtio_mac80211_hwsim` (the default), the guest `wlan0` is a client of
the OpenWrt access point, and OpenWrt routes it to the host:

```
guest wlan0 --(wifi0 LAN, br-wifi0)-- OpenWrt --(wan, eth0/br-lan)-- host
                fd00:cf:25:X::/64                    cvd-wbr       fd00:cf:22::/64   (bridged)
                192.168.99.0/25                      cvd-wifiap-i  fd00:cf:23:i::/64 (non-bridged)
```

The host side (init script `cuttlefish-host-resources`) provides, on the WAN
segment, a router advertisement from a separate RA-only `dnsmasq` and NAT66 to
the upstream network.

## OpenWrt configuration (declarative, ships in the image)

| File | Change | Why |
| --- | --- | --- |
| `shared/config/network` | `config interface 'wan6'`: `device '@wan'`, `proto 'dhcpv6'`, `reqaddress 'try'`, `reqprefix 'no'`, `sourcefilter '0'` | `odhcp6c` takes a SLAAC address and the default route from the host RA. No DHCPv6 prefix delegation exists on the host. `sourcefilter 0`: without it the default route is limited to `from <wan prefix>` and forwarded LAN packets have no route. |
| `shared/config/network` | `globals.ula_prefix 'fd00:cf:25::/48'` (was a random `fd72:5afb:a7cf::/48`), `ip6assign '64'` on `wifi0` and `wifi1` | Fixed, documented LAN prefix; one /64 per Wi-Fi LAN. |
| `shared/config/network` | `br-wifi0` and `br-wifi1`: replace `list ports 'eth0.0'` / `'eth0.1'` and the `eth0.0` / `eth0.1` device sections with `option bridge_empty '1'` and `option macaddr '28:80:88:2A:6D:01'` / `'28:80:88:2A:6D:02'` | Keeps `br-wifi0` and `br-wifi1` up with fixed MAC addresses before `hostapd` attaches `wlan0`/`wlan1` without enslaving 802.1Q VLAN sub-interfaces of `eth0`, so Wi-Fi LAN RAs and broadcast frames do not leak onto `eth0`. |
| `shared/config/dhcp` | `wifi0`/`wifi1`: `ra 'server'`, `ra_slaac '1'`, `ra_default '1'`, `dhcpv6 'disabled'` | `odhcpd` sends RAs with the LAN prefix and RDNSS (the router's LAN address; OpenWrt `dnsmasq` answers, as for IPv4 DHCP DNS). `ra_default 1` is needed because the WAN has only a ULA address; without it `odhcpd` announces router lifetime 0. |
| `shared/config/firewall` | `masq6 '1'` on the `wan` zone | NAT66 on OpenWrt, then NAT66 on the host: same double NAT as IPv4 today. The `wan` zone already lists `wan6`. fw4 already accepts established/related traffic and ICMPv6; no accept-all rule is added. |

IPv4 (`wan` static address from `/proc/cmdline`, `masq '1'`, DHCPv4 on
`wifi0`/`wifi1`) is unchanged. `shared/uci-defaults/0_default_config` is
unchanged.

### Why static UCI and not new `/proc/cmdline` arguments

IPv4 needs per-instance values (`wan_ipaddr`, `wan_gateway`, `wan_broadcast`)
because the host has no DHCPv4 server on the WAN segment, so
`OpenwrtArgsFromConfig()` passes them through `/proc/cmdline` to
`0_default_config`. IPv6 has no per-instance values on the OpenWrt side: the
WAN learns its address and gateway from the host RA, and the LAN prefix is the
same in every OpenWrt instance (it is behind NAT66). The existing static LAN
configuration (`wifi0` `192.168.99.1/25`) lives in `shared/config/network`;
the IPv6 LAN configuration sits next to it. This needs no new host arguments,
no new shell logic, and works for every instance number and both Wi-Fi modes.

The earlier runtime approach in `openwrt_control_server` (a root shell script
sent over LuCI RPC after boot, which flushed nftables and added accept-all
rules) is removed.

## Compatibility

- Older OpenWrt images: no `wan6`; Wi-Fi stays IPv4-only. No IPv4 change.
- New image with a host without IPv6 RA (old host package): `wan6` stays
  without address; the LAN still gets `fd00:cf:25:X::/64` RAs with
  `ra_default 1`, so the guest installs an IPv6 default route that leads
  nowhere (OpenWrt answers with ICMPv6 unreachable). Android does not count a
  ULA-only address as IPv6 provisioning (`LinkAddress.isGlobalPreferred()`
  excludes ULA), so IPv4 stays the provisioned family. IPv4 is unaffected.
- cvdalloc (dynamic) mode: the dynamic-mode patch to `0_default_config`
  (`wan_ip6addr`, `wifi0_ip6addr`) adds a routed prefix on `wifi0`. With this
  change also present, `masq6` NATs that traffic too (it still works, but is
  no longer routed end to end). If both land, the dynamic patch should set
  `firewall.@zone[wan].masq6=0` when `wifi0_ip6addr` is given.

## Local test (no sudo)

Rootfs layout: `openwrt_rootfs_x86_64` is a squashfs (first 74 × 64 KiB) plus
an f2fs overlay holding `upper/etc/config/*` and
`upper/etc/uci-defaults/0_default_config`. `sload.f2fs` does not overwrite
files, so the overlay is dumped and rebuilt with the new files, then appended
to the squashfs part.

Guest checks (after `cmd wifi connect-network`, if Wi-Fi is not already
connected to the default AP `VirtWifi`):

```
adb shell ip -6 addr show wlan0          # inet6 fd00:cf:25:...  scope global dynamic
adb shell ip -6 route show table all | grep '^default.*wlan0'   # proto ra
adb shell ping6 -c3 -I wlan0 2001:4860:4860::8888
adb shell dumpsys connectivity | grep -o 'InterfaceName: wlan0.*DnsAddresses: \[[^]]*\]'
adb shell ping -c3 -I wlan0 8.8.8.8      # IPv4 unchanged
```
