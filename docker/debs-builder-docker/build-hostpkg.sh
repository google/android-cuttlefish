#!/bin/bash

# start preparation
script_location=`realpath -s $(dirname ${BASH_SOURCE[0]})`
cuttlefish_root=$(realpath -s $script_location/../..)

# intended to make "path/ to path"
# while leaving "path" or "/" as is
function remove_trailing_slash() {
    local outdir="$1"
    if [[ "$1" == "/" ]]; then
        echo "${outdir}"
    else
        echo "${outdir%/}"
    fi
}

outdir=$(remove_trailing_slash $1)
shift 1

mkdir -p $outdir

is_mv_debs="true"
[[ "$outdir" == "" ]] && is_mv_debs="false"

#end of preparation

function build() {
    # Add the bazel PPA
    sudo apt install apt-transport-https curl gnupg zip unzip -y
    curl -fsSL https://bazel.build/bazel-release.pub.gpg | gpg --dearmor >bazel-archive-keyring.gpg
    sudo mv bazel-archive-keyring.gpg /usr/share/keyrings
    echo "deb [arch=amd64 signed-by=/usr/share/keyrings/bazel-archive-keyring.gpg] https://storage.googleapis.com/bazel-apt stable jdk1.8" | \
      sudo tee /etc/apt/sources.list.d/bazel.list
    sudo apt update
    local pkg_dirs=( base frontend )
    for dir in "${pkg_dirs[@]}"; do
        pushd $cuttlefish_root/$dir > /dev/null 2>&1
        yes | sudo -E mk-build-deps -i -r -B \
            && sudo -E dpkg-buildpackage -uc -us \
            && sudo chown $(id -u) ../*.deb
        popd > /dev/null 2>&1
    done
}
#sudo apt-get install --no-install-recommends -y -f ${outdir}/cuttlefish-base_*.deb
#sudo apt-get install --no-install-recommends -y -f ${outdir}/cuttlefish-common_*.deb

build
[[ $is_mv_debs == "true" ]] && cp -f $cuttlefish_root/*.deb ${outdir}/
