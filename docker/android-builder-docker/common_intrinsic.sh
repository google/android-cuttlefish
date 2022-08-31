#!/bin/bash
#
# 1: mode
# 2: android_build_top
#
mode=$1

function calc_n() {
    if cat /proc/cpuinfo | \
            egrep "processor[[:space:]:]+[0-9]+$" | \
            tail -1 > /dev/null 2>&1; then
        local n=$(cat /proc/cpuinfo  | \
                      egrep "processor[[:space:]:]+[0-9]+$" | tail -1 | \
                      cut -d ':' -f 2- | egrep -o "[0-9]+")
        echo $(( n + 1 ))
        return
    fi
    echo 2 # default value when there's no /proc/cpuinfo
}

if cat /proc/cpuinfo  | egrep "processor[[:space:]:]+[0-9]+$" > /dev/null 2>&1; then
    n_parallel=$(calc_n)
fi

if (( N_PARALLEL > 0 )); then
    n_parallel=${N_PARALLEL}
fi

echo "cd to $2"
case $mode in
    init)
        echo "running repo init -u $REPO_SRC"
        (cd $2; repo init -u $REPO_SRC; cd -)
        ;;
    sync)
        echo "running repo sync -j $n_parallel"
        (cd $2; repo sync -j $n_parallel; cd -)
        ;;
    build)
        echo "running source ./build/envsetup.sh"
        echo "        lunch $LUNCH_TARGET"
        echo "        m -j $n_parallel"
        cd $2
        source ./build/envsetup.sh
        lunch $LUNCH_TARGET
        m -j $n_parallel
        ;;
    *)
        >&2 echo "unsupported operation"
        exit 10
        ;;
esac
