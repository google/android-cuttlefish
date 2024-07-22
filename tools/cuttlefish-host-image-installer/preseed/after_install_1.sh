#!/bin/sh

apt-get update

# Install necessary packages
apt-get install -y debconf-utils
apt-get install -y ca-certificates
apt-get install -y wget
apt-get install -y git
apt-get install -y python3
apt-get install -y p7zip-full unzip
apt-get install -y iptables ebtables
apt-get install -y curl

# Adjust user groups
adduser vsoc-01 kvm
adduser vsoc-01 render
adduser vsoc-01 video

# Install Linaro GLT. GIG. repo
wget -qO- https://artifacts.codelinaro.org/artifactory/linaro-372-googlelt-gigabyte-ampere-cuttlefish-installer/gigabyte-ampere-cuttlefish-installer/latest/debian/linaro-glt-gig-archive-bookworm.asc | tee /etc/apt/trusted.gpg.d/linaro-glt-gig-archive-bookworm.asc

echo "deb https://artifacts.codelinaro.org/linaro-372-googlelt-gigabyte-ampere-cuttlefish-installer/gigabyte-ampere-cuttlefish-installer/latest/debian bookworm main" | tee /etc/apt/sources.list.d/linaro-glt-gig-archive-bookworm.list

apt -o Apt::Get::Assume-Yes=true -o APT::Color=0 -o DPkgPM::Progress-Fancy=0 \
    update

# Install kernel
#apt-get install -y '^linux-image-6.1.*aosp14-linaro.*' '^linux-headers-6.1.*aosp14-linaro.*'
apt install -y -t bookworm-backports linux-headers-arm64
apt install -y -t bookworm-backports linux-image-arm64

# Install nVidia or AMD GPU driver
nvidia_gpu=$(lspci | grep -i nvidia)
amd_gpu=$(lspci | grep VGA | grep AMD)
if [ "$amd_gpu" != "" ]; then
    # Install amd firmware
    apt-get install -y firmware-amd-graphics
    sed -i 's/GRUB_CMDLINE_LINUX_DEFAULT=\"\(.*\)\"/GRUB_CMDLINE_LINUX_DEFAULT=\"\1 amdgpu.runpm=0 amdgpu.dc=0\"/' /etc/default/grub
    dpkg-reconfigure -fnoninteractive grub-efi-arm64
elif [ "$nvidia_gpu" != "" ]; then
    # Install nvidia driver
    DEBIAN_FRONTEND=noninteractive apt-get install -y -q --force-yes nvidia-open-kernel-dkms
    DEBIAN_FRONTEND=noninteractive apt-get install -y -q --force-yes nvidia-driver
    DEBIAN_FRONTEND=noninteractive apt-get install -y -q --force-yes firmware-misc-nonfree
fi


# Install android cuttlefish packages
apt-get install -y '^cuttlefish-.*'
adduser vsoc-01 cvdnetwork

# Install metapackage
apt-get install -y metapackage-linaro-gigamp

# Extra tools
cd /root
git clone https://github.com/matthuisman/gdrivedl.git
cd -

# Use iptables-legacy
update-alternatives --set iptables /usr/sbin/iptables-legacy

# Install network manager
apt-get install -y network-manager

# Network-manager workaround
rm -f '/etc/NetworkManager/system-connections/Wired connection 1'

# Install Docker container
# Add Docker's official GPG key:
apt-get update
curl -fsSL https://download.docker.com/linux/debian/gpg -o /etc/apt/trusted.gpg.d/docker.asc
chmod a+r /etc/apt/trusted.gpg.d/docker.asc

# Add the repository to Apt sources:
echo \
  "deb [arch=$(dpkg --print-architecture)] https://download.docker.com/linux/debian $(. /etc/os-release && echo "$VERSION_CODENAME") stable" | \
  tee /etc/apt/sources.list.d/docker.list > /dev/null
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y -q docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
usermod -aG docker vsoc-01

# Inastall nvidia-container-toolkit
curl -fsSL https://nvidia.github.io/libnvidia-container/gpgkey | gpg --dearmor -o /etc/apt/trusted.gpg.d/nvidia-container-toolkit-keyring.gpg \
&& curl -s -L https://nvidia.github.io/libnvidia-container/stable/deb/nvidia-container-toolkit.list | \
  tee /etc/apt/sources.list.d/nvidia-container-toolkit.list
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y  -q --force-yes nvidia-container-toolkit

# CO server build needs GO lang
DEBIAN_FRONTEND=noninteractive apt-get install -y -q --force-yes golang

# Build docker image
#mydir=$(pwd)
#pushd $mydir
#cd /home/vsoc-01
#sudo -H -u vsoc-01 sh -c 'git clone https://github.com/google/android-cuttlefish.git'
#cd android-cuttlefish/docker/orchestration
#sudo -H -u vsoc-01 bash -c '/bin/bash build.sh'
#popd
