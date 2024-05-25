#!/bin/sh

CUTTLEFISH_GIT_URL_DEFAULT="https://github.com/google/android-cuttlefish.git"
CUTTLEFISH_GIT_BRANCH_DEFAULT="stable"

if [ x"$CUTTLEFISH_GIT_URL" = x ]; then
    CUTTLEFISH_GIT_URL="$CUTTLEFISH_GIT_URL_DEFAULT"
fi

if [ x"$CUTTLEFISH_GIT_BRANCH" = x ]; then
    CUTTLEFISH_GIT_BRANCH="$CUTTLEFISH_GIT_BRANCH_DEFAULT"
fi

git clone --branch="${CUTTLEFISH_GIT_BRANCH}" "${CUTTLEFISH_GIT_URL}" android-cuttlefish
cd android-cuttlefish

if [ x"$DEBEMAIL" = x ]; then
    export DEBEMAIL="glt-noreply@linaro.org"
fi

if [ x"$DEBFULLNAME" = x ]; then
    export DEBFULLNAME="Linaro GLT Deb"
fi

if [ x"${CI_PIPELINE_ID}" = x ]; then
    export CI_PIPELINE_ID=1
fi

for subdir in base frontend; do
    cd ${subdir}
    UPSTREAM_VERSION=$(dpkg-parsechangelog -S Version)
    dch -v "${UPSTREAM_VERSION}"."linaro${CI_PIPELINE_ID}" "Linaro build"
    dch -r "Linaro build"
    mk-build-deps --install --remove --tool='apt-get -o Debug::pkgProblemResolver=yes --no-install-recommends --yes' debian/control
    dpkg-buildpackage -d -uc -us
    cd -
done
