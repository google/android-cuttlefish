#!/bin/sh

SELFPID=$$
renice 10 -p "$SELFPID"
ionice -c 3 -p "$SELFPID"

TDIR=/tmp/d19
mkdir -p ${TDIR}

cat <<EOF > ${TDIR}/buildscript1
#!/bin/sh

apt-get install -y sudo
apt-get install -y git ca-certificates less
apt-get install -y fakeroot
apt-get install -y devscripts equivs
apt-get install -y ubuntu-dev-tools

mkdir /tmp/r1
cd /tmp/r1
pull-debian-source repo
cd repo-*
mk-build-deps --install --remove --tool='apt-get -o Debug::pkgProblemResolver=yes --no-install-recommends --yes' debian/control
debuild
cd ..

apt -o Apt::Get::Assume-Yes=true -o APT::Color=0 -o DPkgPM::Progress-Fancy=0 \
    install ./*.deb

cd /root
EOF

cat kernel_dependencies.sh >> ${TDIR}/buildscript1
cat kernel_download.sh >> ${TDIR}/buildscript1
cat kernel_build_deb.sh >> ${TDIR}/buildscript1

cat <<EOF >> ${TDIR}/buildscript1
cp -f /root/kernel-build-space/buildresult/*.deb ${TDIR}
EOF

chmod a+rx ${TDIR}/buildscript1

cd ${TDIR}
pbuilder-dist stable arm64 execute --bindmounts "${TDIR}" -- buildscript1
