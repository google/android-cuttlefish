#!/bin/sh

#
# Copyright (C) 2018 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

default_instance_number() {
    if [[ "${USER::5}" == "vsoc-" ]]; then
        echo "${USER: -2}"
    else
        echo "01"
    fi
}
CUTTLEFISH_INSTANCE="${CUTTLEFISH_INSTANCE:-$(default_instance_number)}"
default_instance_name="cvd-${CUTTLEFISH_INSTANCE}"
default_uuid="699acfc4-c8c4-11e7-882b-5065f31dc1${CUTTLEFISH_INSTANCE}"
default_dir="${HOME}/runfiles"
default_mobile_tap_name="cvd-mtap-${CUTTLEFISH_INSTANCE}"

if [[ -z "${ivshmem_vector_count}" ]]; then
    echo "The required ivshmem_vector_count environment variable is not set" >&2
    exit 1
fi

exec "${qemu_binary=/usr/bin/qemu-system-x86_64}" \
    -enable-kvm \
    -name "guest=${instance_name=${default_instance_name}},debug-threads=on" \
    -machine "pc-i440fx-2.8,accel=kvm,usb=off,dump-guest-core=off" \
    -m "${memory_mb=2048}" \
    -realtime mlock=off \
    -smp "${cpus=2},sockets=${cpus=2},cores=1,threads=1" \
    -uuid "${uuid=${default_uuid}}"\
    -display none \
    -no-user-config \
    -nodefaults \
    -chardev "socket,id=charmonitor,path=${monitor_path=${default_dir}/qemu_monitor.sock},server,nowait" \
    -mon "chardev=charmonitor,id=monitor,mode=control" \
    -rtc "base=utc" \
    -no-shutdown \
    -boot "strict=on" \
    -kernel "${kernel_image_path=${HOME}/kernel}" \
    -initrd "${ramdisk_image_path=${HOME}/ramdisk.img}" \
    -append "${kernel_args="loop.max_part=7 console=ttyS0 androidboot.console=ttyS1 androidboot.hardware=vsoc enforcing=0 audit=1 androidboot.selinux=permissive mac80211_hwsim.radios=0 security=selinux buildvariant=userdebug  androidboot.serialno=CUTTLEFISHCVD01 androidboot.lcd_density=160"}" \
    -dtb "${dtb_path=${HOME}/config/cuttlefish.dtb}" \
    -device "piix3-usb-uhci,id=usb,bus=pci.0,addr=0x1.0x2" \
    -device "virtio-serial-pci,id=virtio-serial0,bus=pci.0,addr=0x3" \
    -drive "file=${system_image_path=${HOME}/system.img},format=raw,if=none,id=drive-virtio-disk0,aio=threads" \
    -device "virtio-blk-pci,scsi=off,bus=pci.0,addr=0x4,drive=drive-virtio-disk0,id=virtio-disk0,bootindex=1" \
    -drive "file=${data_image_path=${HOME}/userdata.img},format=raw,if=none,id=drive-virtio-disk1,aio=threads" \
    -device "virtio-blk-pci,scsi=off,bus=pci.0,addr=0x5,drive=drive-virtio-disk1,id=virtio-disk1" \
    -drive "file=${cache_image_path=${HOME}/cache.img},format=raw,if=none,id=drive-virtio-disk2,aio=threads" \
    -device "virtio-blk-pci,scsi=off,bus=pci.0,addr=0x6,drive=drive-virtio-disk2,id=virtio-disk2" \
    -drive "file=${vendor_image_path=${HOME}/vendor.img},format=raw,if=none,id=drive-virtio-disk3,aio=threads" \
    -device "virtio-blk-pci,scsi=off,bus=pci.0,addr=0x7,drive=drive-virtio-disk3,id=virtio-disk3" \
    -netdev "tap,id=hostnet0,ifname=${mobile_tap_name=${default_mobile_tap_name}},script=no,downscript=no" \
    -device "virtio-net-pci,netdev=hostnet0,id=net0,mac=00:43:56:44:01:01,bus=pci.0,addr=0x2" \
    -chardev "socket,id=charserial0,path=${kernel_log_socket_name=${default_dir}/kernel-log}" \
    -device "isa-serial,chardev=charserial0,id=serial0" \
    -chardev "socket,id=charserial1,path=${console_path=${default_dir}/console},server,nowait" \
    -device "isa-serial,chardev=charserial1,id=serial1" \
    -chardev "file,id=charchannel0,path=${logcat_path=${default_dir}/logcat},append=on" \
    -device "virtserialport,bus=virtio-serial0.0,nr=1,chardev=charchannel0,id=channel0,name=cf-logcat" \
    -device "virtio-balloon-pci,id=balloon0,bus=pci.0,addr=0x8" \
    -object "rng-random,id=objrng0,filename=/dev/urandom" \
    -device "virtio-rng-pci,rng=objrng0,id=rng0,max-bytes=1024,period=2000,bus=pci.0,addr=0x9" \
    -chardev "socket,path=${ivshmem_qemu_socket_path=${default_dir}/ivshmem_socket_qemu},id=ivsocket" \
    -device "ivshmem-doorbell,chardev=ivsocket,vectors=${ivshmem_vector_count}" \
    -cpu host \
    -msg "timestamp=on"
