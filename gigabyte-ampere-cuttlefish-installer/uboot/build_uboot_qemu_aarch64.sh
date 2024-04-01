#!/bin/sh

set +e

SELFPID=$$
renice 10 -p "$SELFPID"
ionice -c 3 -p "$SELFPID"

set -e

B=../uboot_build_place
rm -rf "$B"
mkdir -p "$B"

B=$(realpath "${B}")

export CROSS_COMPILE=aarch64-linux-gnu-

cd "$S"

make O="$B" qemu_arm64_defconfig
cat <<EOF > "${B}"/extraconfig
CONFIG_PROT_TCP=y
CONFIG_PROT_TCP_SACK=y
CONFIG_CMD_WGET=y
CONFIG_UDP_FUNCTION_FASTBOOT=y
CONFIG_TCP_FUNCTION_FASTBOOT=y
CONFIG_IPV6=y
CONFIG_VIRTIO_CONSOLE=y
CONFIG_FASTBOOT_BUF_ADDR=0x08000000
CONFIG_CMD_EFIDEBUG=y
CONFIG_DISTRO_DEFAULTS=y
EOF

./scripts/kconfig/merge_config.sh -O ${B} ${B}/.config ${B}/extraconfig

make O="$B"
