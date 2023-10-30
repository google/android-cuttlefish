#!/bin/sh

TDIR=`pwd`/cuttlefish-common-buildplace
mkdir -p "${TDIR}"

cat <<EOF > "${TDIR}"/buildscript_cf_1
#!/bin/sh

apt-get install -y git ca-certificates less
apt-get install -y debhelper devscripts cdbs dpkg-dev equivs fakeroot
apt-get install -y build-essential autoconf automake
apt-get install -y flex bison libmnl-dev
apt-get install -y libnetfilter-conntrack-dev libnfnetlink-dev
apt-get install -y libnftnl-dev libtool

mkdir -p /tmp/b1

cd /tmp/b1

git clone https://github.com/google/android-cuttlefish.git
cd android-cuttlefish

for subdir in base frontend; do
    cd \${subdir}
    mk-build-deps --install --remove --tool='apt-get -o Debug::pkgProblemResolver=yes --no-install-recommends --yes' debian/control
    dpkg-buildpackage -d -uc -us
    cd -
done
cp -f *.deb ${TDIR}
EOF

chmod a+rx "${TDIR}"/buildscript_cf_1

cd "${TDIR}"
pbuilder-dist stable arm64 execute --bindmounts "${TDIR}" -- buildscript_cf_1 
