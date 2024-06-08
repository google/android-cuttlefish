#!/bin/sh

TDIR=`pwd`/cuttlefish-common-buildplace
mkdir -p "${TDIR}"

cat <<EOF > "${TDIR}"/buildscript_cf_1
#!/bin/sh

apt-get install -y git ca-certificates less
apt-get install -y build-essential
apt-get install -y devscripts equivs fakeroot dpkg-dev

apt install wget
wget "https://github.com/bazelbuild/bazelisk/releases/download/v1.19.0/bazelisk-linux-arm64"
mv bazelisk-linux-arm64 /usr/local/bin/bazel
chmod 0755 /usr/local/bin/bazel
export PATH=$PATH:/usr/local/bin

mkdir -p /tmp/b1

cd /tmp/b1
EOF

if [ x"$DEBEMAIL" != x ]; then
    cat <<EOF >> "${TDIR}"/buildscript_cf_1
export DEBEMAIL="${DEBEMAIL}"
EOF
fi

if [ x"$DEBFULLNAME" != x ]; then
    cat <<EOF >> "${TDIR}"/buildscript_cf_1
export DEBFULLNAME="${DEBFULLNAME}"
EOF
fi

if [ x"${CI_PIPELINE_ID}" != x ]; then
    cat <<EOF >> "${TDIR}"/buildscript_cf_1
export CI_PIPELINE_ID="${CI_PIPELINE_ID}"
EOF
fi

cat build_cf_packages_native.sh >> "${TDIR}"/buildscript_cf_1

cat <<EOF >> "${TDIR}"/buildscript_cf_1
cp -f *.deb ${TDIR}
EOF

chmod a+rx "${TDIR}"/buildscript_cf_1

cd "${TDIR}"
pbuilder-dist stable arm64 execute --bindmounts "${TDIR}" -- buildscript_cf_1 
