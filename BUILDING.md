gcloud compute instances create --image-family=ubuntu-1704 --image-project=ubuntu-os-cloud --machine-type=n1-standard-8 ${USER}-builder

gcloud compute disks create \
    --image-family=ubuntu-1704 --image-project=ubuntu-os-cloud --size=300GB \
  ${USER}-source

wait_for_instance() {
  alive=""
  while [[ -z "${alive}" ]]; do
    sleep 5
    alive="$(gcloud compute ssh "$@" -- uptime || true)"
  done
}

fixdirs() {
  path="$1"
  if [[ -z "${path}" ]]; then
     return
   fi
   if [[ "${path:0:1}" != / ]]; then
    path="$(pwd)/${path}"
   fi
   while [[ "${path}" != / ]]; do
     if [[ -n "${FLAGS_dir_acl}" ]]; then
       setfacl -m "${FLAGS_dir_acl}" "${path}" 2>/dev/null
     fi
     path="$(dirname "${path}")"
   done
}

wait_for_instance ${USER}-builder

gcloud compute instances attach-disk ${USER}-builder --disk=${USER}-source

gcloud compute ssh builder@${USER}-builder

# Initial machine setup

sudo e2fsck -f /dev/sdb1
sudo resize2fs /dev/sdb1

sudo mkdir /mnt/builder
sudo mount /dev/sdb1 /mnt/builder
sudo mount -t sysfs none /mnt/builder/sys
sudo mount -t proc none /mnt/builder/proc
sudo mount --bind /dev/ /mnt/builder/dev
sudo mount --bind /dev/pts /mnt/builder/dev/pts
sudo mount --bind /run /mnt/builder/run
sudo chroot /mnt/builder /bin/bash

sudo apt-get update
sudo apt install -y openjdk-8-jdk git-core gnupg flex bison gperf build-essential zip curl zlib1g-dev gcc-multilib g++-multilib libc6-dev-i386 lib32ncurses5-dev x11proto-core-dev libx11-dev lib32z-dev ccache libgl1-mesa-dev libxml2-utils xsltproc unzip debhelper ubuntu-dev-tools equivs

groupadd libvirt
groupadd google-sudoers
useradd -m vsoc-01 -G libvirt,plugdev,google-sudoers
su - vsoc-01
git clone https://github.com/google/android-cuttlefish.git
git clone https://android.googlesource.com/kernel/x86_64.git
dpkg-source -b android-cuttlefish
exit

yes | sudo mk-build-deps -i /home/vsoc-01/cuttlefish-common_0.2.dsc -t apt-get

su - vsoc-01
cd android-cuttlefish/
dpkg-buildpackage -uc -us
exit

apt install -y /home/vsoc-01/*.deb

su - vsoc-01
mkdir ~/bin
PATH=~/bin:$PATH
curl https://storage.googleapis.com/git-repo-downloads/repo > ~/bin/repo
chmod a+x ~/bin/repo
mkdir aosp-master
cd aosp-master
git config --global user.email ${USER}@nomail.com
git config --global user.name "No Name"
repo init -u https://android.googlesource.com/platform/manifest
repo sync -j 8 -q
. build/envsetup.sh
lunch aosp_cf_x86_phone-userdebug
time make -j dist # 112 minutes
exit
exit
exit


gcloud compute instances delete -q ${USER}-builder

gcloud compute images create cuttlefish-built-2017-01-11 \
    --source-disk=${USER}-source \
    --licenses=https://www.googleapis.com/compute/v1/projects/vm-options/global/licenses/enable-vmx
gcloud compute disks delete -q ${USER}-source
gcloud compute instances create ${USER}-test --image=cuttlefish-built-2017-01-11 --machine-type=n1-standard-4

gcloud compute ssh vsoc-01@${USER}-test
FLAGS_dir_acl=u:libvirt-qemu:rx
FLAGS_file_acl=u:libvirt-qemu:rw
FLAGS_instance=1
FLAGS_cpus=2
FLAGS_memory=2048
FLAGS_unpacker=/usr/lib/cuttlefish-common/bin/unpack_boot_image.py
cd aosp-master
. build/envsetup.sh
lunch aosp_cf_x86_phone-userdebug
boot_dir="${ANDROID_PRODUCT_OUT}"
tmpdir="$(mktemp -d)"
"${FLAGS_unpacker}" -dest "${tmpdir}" -boot_img "${boot_dir}/boot.img"
cmdline_path="${tmpdir}/cmdline"
vsoc_mem_path=($(find "${boot_dir}" -name vsoc_mem.json -print ) )
vsoc_mem_path="${vsoc_mem_path[0]}"

files=(
  "${boot_dir}/kernel"
  "${boot_dir}/cache.img"
  "${boot_dir}/ramdisk.img"
  "${boot_dir}/system.img"
  "${boot_dir}/userdata.img"
  "${boot_dir}/vendor.img"
  "${cmdline_path}"
  "${vsoc_mem_path}"
)

for f in "${files[@]}"; do
  if [[ -n "${FLAGS_file_acl}" ]]; then
    setfacl -m "${FLAGS_file_acl}"  "$f"
  fi
  fixdirs "$(dirname "${f}")"
  fixdirs "$(dirname "$(realpath "${f}")" )"
done

out/host/linux-x86/bin/launch_cvd \
  -cpus "${FLAGS_cpus}" \
  -instance "${FLAGS_instance}" \
  -kernel "${boot_dir}/kernel" \
  -kernel_command_line "${cmdline_path}" \
  -layout "${vsoc_mem_path}" \
  -memory_mb "${FLAGS_memory}" \
  -system_image_dir "${boot_dir}" \
  -log_xml
