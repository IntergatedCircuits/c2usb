#!/bin/sh
set -eu

HOST_IP=${HOST_IP:-192.0.2.2/24}
DEVICE_IP=${DEVICE_IP:-192.0.2.1}
NETWORK=${NETWORK:-192.0.2.0/24}
HOST_MAC=${HOST_MAC:-00:00:5e:00:53:01}

if [ "$#" -gt 1 ]; then
    printf 'Usage: %s [interface]\n' "$0" >&2
    exit 2
fi

if [ "$#" -eq 1 ]; then
    interface=$1
else
    interface=''
    for candidate in /sys/class/net/*; do
        [ -e "$candidate/address" ] || continue
        if [ "$(cat "$candidate/address")" = "$HOST_MAC" ]; then
            interface=${candidate##*/}
            break
        fi
    done
fi

if [ -z "$interface" ] || [ ! -e "/sys/class/net/$interface" ]; then
    printf 'Could not find USB NCM interface (MAC %s).\n' "$HOST_MAC" >&2
    printf 'Pass interface name explicitly: %s enx...\n' "$0" >&2
    exit 1
fi

printf 'Configuring %s for USB NCM...\n' "$interface"
sudo ip link set "$interface" up
sudo ip address replace "$HOST_IP" dev "$interface"
sudo ip route replace "$NETWORK" dev "$interface" src "${HOST_IP%/*}"

ip address show dev "$interface"
ip route get "$DEVICE_IP"
printf 'USB NCM network ready. Test with: ping -I %s %s\n' "$interface" "$DEVICE_IP"
