#!/bin/bash
# boot <label> <cap-seconds> <qemu accel args...>
#
# Boots the disks assemble_cvd produced (in ./disks) the way cuttlefish's
# qemu_manager would, minus the host-side helpers (vhost-user devices,
# vsock, tap networking, host daemons), and prints how long each boot
# stage took to show up on the kernel console. Returns 0 only if the
# guest reported VIRTUAL_DEVICE_BOOT_COMPLETED within the cap.
set -u
label=$1 cap=$2
shift 2
here=$(dirname "$0")
cpus=$(nproc 2>/dev/null || sysctl -n hw.ncpu)
stages=('init first stage started' 'init second stage started'
        "starting service 'zygote'" 'VIRTUAL_DEVICE_BOOT_COMPLETED'
        'VIRTUAL_DEVICE_BOOT_FAILED' 'Kernel panic')
start=$(date +%s)
rm -f "serial-$label.log" "kernel-$label.log" "logcat-$label.log"

# /dev/hvc0 is the kernel log, hvc1 the (disabled) console, hvc2 logcat;
# the rest talk to host daemons, which responder.py stands in for.
hvcs=() socks=()
for i in $(seq 0 30); do
  case $i in
    0) hvcs+=(-chardev "file,id=hvc$i,path=kernel-$label.log");;
    2) hvcs+=(-chardev "file,id=hvc$i,path=logcat-$label.log");;
    *) hvcs+=(-chardev "socket,id=hvc$i,path=hvc$i.sock")
       socks+=("hvc$i.sock");;
  esac
  hvcs+=(-device "virtconsole,bus=virtio-serial.0,chardev=hvc$i")
done
rm -f "${socks[@]}"
python3 "$here/responder.py" "${socks[@]}" > "responder-$label.log" 2>&1 &
responder=$!
for s in "${socks[@]}"; do until [ -S "$s" ]; do sleep 0.1; done; done

# The bootconfig names the boot devices by PCI slot, so the gpu (2),
# virtio-serial (3) and disks (4-6) have to come first and in order.
disks=()
i=0
for disk in os_composite persistent_composite sdcard; do
  disks+=(-drive "file=disks/$disk.img,format=raw,if=none,aio=threads,\
id=drive-virtio-disk$i,snapshot=on"
          -device "virtio-blk-pci-non-transitional,id=virtio-disk$i,\
drive=drive-virtio-disk$i$([ $i = 0 ] && echo ,bootindex=1)")
  i=$((i + 1))
done
pmems=()
i=0
for pmem in access-kregistry hwcomposer-pmem; do
  pmems+=(-object "memory-backend-file,id=objpmem$((i + 1)),share=on,\
mem-path=disks/$pmem,size=2097152"
          -device "virtio-pmem-pci,disable-legacy=on,id=pmem$i,\
memdev=objpmem$((i + 1))")
  i=$((i + 1))
done
qemu-system-x86_64 "$@" \
  -machine pc,nvdimm=on,usb=off \
  -m size=4096M,maxmem=4102M,slots=2 -overcommit mem-lock=off \
  -smp "$cpus,cores=$cpus,threads=1" \
  -no-user-config -nodefaults -no-shutdown -no-reboot \
  -rtc base=utc -boot strict=on -display none \
  -device virtio-gpu-pci,id=gpu0,addr=02.0,xres=720,yres=1280 \
  -serial "file:serial-$label.log" \
  -device virtio-serial-pci-non-transitional,max_ports=31,id=virtio-serial \
  "${hvcs[@]}" "${disks[@]}" \
  -object memory-backend-file,id=objpmem0,share=on,mem-path=disks/pstore,size=2097152 \
  -device nvdimm,memdev=objpmem0,id=ramoops \
  "${pmems[@]}" \
  -object rng-random,id=objrng0,filename=/dev/urandom \
  -device virtio-rng-pci-non-transitional,rng=objrng0,id=rng0,max-bytes=1024,period=2000 \
  -device virtio-mouse-pci,disable-legacy=on \
  -device virtio-keyboard-pci,disable-legacy=on \
  -device virtio-balloon-pci-non-transitional,id=balloon0 \
  -netdev user,id=hostnet0 -netdev user,id=hostnet1 \
  -device virtio-net-pci-non-transitional,netdev=hostnet0,id=net0,mac=00:1a:11:e0:cf:00 \
  -device virtio-net-pci-non-transitional,netdev=hostnet1,id=net1,mac=00:1a:11:e1:cf:00 \
  -device AC97,audiodev=audio_none -audiodev driver=none,id=audio_none \
  -device qemu-xhci,id=xhci \
  -drive if=pflash,format=raw,readonly=on,file=disks/bootloader.qemu \
  -drive if=pflash,format=raw,file=disks/pflash.img,snapshot=on \
  -msg timestamp=on -d guest_errors,cpu_reset -D "qemu-$label.dbg" \
  2> "qemu-$label.err" &
qemu=$!

finish() {
  kill "$qemu" "$responder" 2>/dev/null
  exit "$1"
}
seen=()
rc=1
for _ in $(seq "$cap"); do
  for stage in "${stages[@]}"; do
    case " ${seen[*]-} " in *" $stage "*) continue;; esac
    if grep -Fq "$stage" "serial-$label.log" "kernel-$label.log" \
        2>/dev/null; then
      echo "$label: '$stage' after $(( $(date +%s) - start ))s"
      seen+=("$stage")
      [ "$stage" = VIRTUAL_DEVICE_BOOT_COMPLETED ] && rc=0
      case $stage in
        VIRTUAL_DEVICE_BOOT_*|'Kernel panic') finish $rc;;
      esac
    fi
  done
  if ! kill -0 "$qemu" 2>/dev/null; then
    echo "$label: qemu exited:"
    cat "qemu-$label.err"
    finish 1
  fi
  sleep 1
done
echo "$label: gave up after ${cap}s"
finish 1
