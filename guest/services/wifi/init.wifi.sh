#!/vendor/bin/sh

wifi_mac_prefix=`getprop ro.boot.wifi_mac_prefix`
if [ -n "$wifi_mac_prefix" ]; then
    /vendor/bin/mac80211_create_radios 2 $wifi_mac_prefix || exit 1
fi
NAMESPACE="router"

PID=$(</data/vendor/var/run/netns/${NAMESPACE}.pid)

/vendor/bin/ip link set eth0 netns ${PID}

execns ${NAMESPACE} /vendor/bin/ip link set eth0 up

execns ${NAMESPACE} /vendor/bin/ip link set wlan1 up

/vendor/bin/iw phy phy2 set netns $PID

setprop ctl.start netmgr

if [ ! -f /data/vendor/wifi/hostapd/hostapd.conf ]; then
    cp /vendor/etc/simulated_hostapd.conf /data/vendor/wifi/hostapd/hostapd.conf
    chown wifi:wifi /data/vendor/wifi/hostapd/hostapd.conf
    chmod 660 /data/vendor/wifi/hostapd/hostapd.conf
fi

setprop ctl.start emu_hostapd

