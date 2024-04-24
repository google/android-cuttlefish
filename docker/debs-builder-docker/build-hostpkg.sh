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
