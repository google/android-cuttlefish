set -x
set -o errexit

arch=$(uname -m)
nvidia_arch=${arch}
[ "${arch}" = "x86_64" ] && arch=amd64
[ "${arch}" = "aarch64" ] && arch=arm64

# NVIDIA driver needs dkms which requires /dev/fd
if [ ! -d /dev/fd ]; then
  ln -s /proc/self/fd /dev/fd
fi

# Using "Depends:" is more reliable than "Version:", because it works for
# backported ("bpo") kernels as well. NOTE: "Package" can be used instead
# if we don't install the metapackage ("linux-image-cloud-${arch}") but a
# specific version in the future
kmodver=$(dpkg -s linux-image-cloud-${arch} | grep ^Depends: | \
          cut -d: -f2 | cut -d" " -f2 | sed 's/linux-image-//')

apt-get install -y wget
# Install headers from backports, to match the linux-image
apt-get install -y -t bullseye-backports $(echo linux-headers-${kmodver})
# Dependencies for nvidia-installer
apt-get install -y dkms libglvnd-dev libc6-dev pkg-config

nvidia_version=515.65.01

wget -q https://us.download.nvidia.com/tesla/${nvidia_version}/NVIDIA-Linux-${nvidia_arch}-${nvidia_version}.run
chmod a+x NVIDIA-Linux-${nvidia_arch}-${nvidia_version}.run
./NVIDIA-Linux-${nvidia_arch}-${nvidia_version}.run -x
NVIDIA-Linux-${nvidia_arch}-${nvidia_version}/nvidia-installer --silent --no-install-compat32-libs --no-backup --no-wine-files --install-libglvnd --dkms -k "${kmodver}"

