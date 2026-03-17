#!/bin/sh

CURRENTARCH=$(dpkg-architecture -qDEB_BUILD_ARCH)

RESULTDIR=$(mktemp -d)
cat >"${RESULTDIR}/runscript" <<EOF
#!/bin/sh

apt -o Apt::Get::Assume-Yes=true -o APT::Color=0 -o DPkgPM::Progress-Fancy=0 install sudo
sudo apt -o Apt::Get::Assume-Yes=true -o APT::Color=0 -o DPkgPM::Progress-Fancy=0 install zstd
sudo apt -o Apt::Get::Assume-Yes=true -o APT::Color=0 -o DPkgPM::Progress-Fancy=0 install initramfs-tools
sudo apt -o Apt::Get::Assume-Yes=true -o APT::Color=0 -o DPkgPM::Progress-Fancy=0 install lvm2
DEBIAN_FRONTEND=noninteractive sudo -E apt -o Apt::Get::Assume-Yes=true -o APT::Color=0 -o DPkgPM::Progress-Fancy=0 -o Dpkg::Options::="--force-confdef" -o Dpkg::Options::="--force-confnew" install debian-cloud-images-packages
cp /boot/initrd.img-* "${RESULTDIR}"

EOF

DEBIAN_DISTRIBUTION="$(lsb_release -c -s)"
if [ x"${CURRENTARCH}" != x"arm64" ]; then
    # Use pbuilder-dist (with qemu-user-static).
    TMPDIR=$(mktemp -d)
    export PBUILDFOLDER="${TMPDIR}"
    pbuilder-dist "${DEBIAN_DISTRIBUTION}" arm64 create
    pbuilder-dist "${DEBIAN_DISTRIBUTION}" arm64 execute --bindmounts "${RESULTDIR}" -- "${RESULTDIR}/runscript"
    rm -rf "${TMPDIR}"
else
    # Use pbuilder (without qemu, natively).
    sudo -E pbuilder create --distribution "${DEBIAN_DISTRIBUTION}" --othermirror "deb http://security.debian.org/debian-security ${DEBIAN_DISTRIBUTION}-security main" --basetgz "${RESULTDIR}/base.tgz"
    sudo -E pbuilder execute --basetgz "${RESULTDIR}/base.tgz" --bindmounts "${RESULTDIR}" -- "${RESULTDIR}/runscript"
fi
cp -f "${RESULTDIR}"/initrd.img-* .
rm -rf "${RESULTDIR}"
