#!/vendor/bin/sh

wifi_mac_prefix=`getprop ro.boot.wifi_mac_prefix`
if [ -n "$wifi_mac_prefix" ]; then
    /vendor/bin/mac80211_create_radios 2 $wifi_mac_prefix || exit 1
fi

