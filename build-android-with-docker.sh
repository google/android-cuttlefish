#!/bin/bash

# for now, the script will build docker image as needed
# and gives a bash shell inside the container, so that
# the user can run source, lunch & m commands to build
#
# TODO: support commands like
#   intrinsic commands: init, sync, build, img_build
#   running a host script inside docker container
#   running a guest command inside docker container

# to print multi-lined helper for shflags
function multiline_helper {
    local aligner=''
    for cnt in `seq 0 $1`; do
        aligner+=' '
    done

    local -n msgs=$2
    echo ${msgs[0]}
    for msg in "${msgs[@]:1}"; do
        echo "${aligner}""$msg"
    done
}

script_name=$(basename $0)

# android_src_mnt helper message that is multi-lined
android_src_mnt_helper=("src_dir:mount_point | src_dir | :mount_point | <an empty string>")
android_src_mnt_helper+=("  src_dir on the host will be mounted mount_pointer on the docker container")
android_src_mnt_helper+=("    e.g. /home/$USER/aosp03:/home/vsoc-01/aosp")
android_src_mnt_helper+=("    This will mount /home/$USER/aosp03 to /home/vsoc-01/aosp in the container")
android_src_mnt_helper+=("  if mount_point is missing, it is the src_dir with $HOME if any being replaced with /home/vsoc-01")
android_src_mnt_helper+=("  if src_dir is missing, it should be given in the environment variable ANDROID_BUILD_TOP.")

source "shflags"

DEFINE_boolean rebuild_docker_img false "Rebuild cuttlefish-android-builder image" ""
DEFINE_boolean share_gitconfig true "Allow the docker container to use the host gitconfig"
DEFINE_string android_src_mntptr \
              "" \
              "$(multiline_helper 24 android_src_mnt_helper)" \

FLAGS "$@" || exit 1
eval set -- "${FLAGS_ARGV}"

# As FLAGS_ARGV is not the best for us to process, we convert it into a bash array
# Especially, iterating over or passing FLAGS_ARGV may not work as expected
# when an argument is surrounded by "" and has space(s) inside: "a b c"
arg_list=()
function parse_cmds() {
    # a finite state machine
    local state="skip" # and 'record'
    local as_str="${FLAGS_ARGV}"
    local word=""
    for i in $(seq 1 ${#as_str}); do
        local input="${as_str:i-1:1}"
        case $input in
            [[:space:]] )
                case $state in
                    "skip") ;;
                    "record") word+="$input" ;;
                esac
                ;;
            \')
                case $state in
                    "skip") state="record" ;;
                    *) # record
                        state="skip"
                        arg_list+=( "$word" )
                        word=""
                        ;;
                esac
                ;;
            *)  word+="$input"
                ;;
        esac
    done
}

parse_cmds
# "${arg_list[@]}" is ready to be used

# pick up src dir and its mnt point
android_src_host_dir=""
android_src_mnt_dir=""

# util func used by calc_mnt_point
function no_src_dir2mount() {
    >&2 echo "Error: --android_src_mntptr should be given and indicate at least source directory. try:"
    >&2 echo "       $0 -h"
    >&2 echo "       Alternatively, set ANDROID_BUILD_TOP to the root of the Android source directory."
    exit 10
}

#util func used by calc_mnt_point
function default_mnt_dir() {
    local srcdir="$(realpath $1)"
    local home=$(echo $HOME | sed -e 's/\//\\\//g')
    echo ${srcdir} | sed -e "s/$home/\/home\/vsoc-01/g"
}

# output: android_src_{host,mnt}_dir
# input: FLAGS_android_src_mntptr and/or ANDROID_BUILD_TOP env variable
function calc_mnt_point() {
    local arg=${FLAGS_android_src_mntptr}
    local tk0=""
    local tk1=""
    if echo ${arg} | egrep ':' > /dev/null 2>&1; then
        # "src:mnt", "src:", ":mnt", ":"
        tk0="$(echo ${arg} | cut -d ':' -f1)"
        tk1="$(echo ${arg} | cut -d ':' -f2-)"
    else
        if test -n ${arg}; then
            # "src"
            tk0="${arg}"
        fi
    fi

    if test -z "${tk0}"; then
        tk0="${ANDROID_BUILD_TOP}"
        if test -z "${tk0}"; then
            no_src_dir2mount
        fi
    fi
    android_src_host_dir="$(realpath ${tk0})"
    if test -z ${tk1}; then
        tk1=$(default_mnt_dir "${android_src_host_dir}")
    fi
    android_src_mnt_dir="${tk1}"

    if [[ "${android_src_mnt_dir}" == "/home/vsoc-01" ]]; then
        >&2 echo "Error: We don't allow the source to be mounted exactly at /home/vsoc-01"
        >&2 echo "       Please consider to mount it at a subdirectory of /home/vsoc-01"
        exit 9
    fi
}
calc_mnt_point

# Output
cf_builder_img_name="cuttlefish-android-builder"
cf_builder_img_tag="latest"

# see Dockerfile.builder, the target name must match
cf_builder_img_target="cuttlefish-android-builder"

# build docker image when required
function is_rebuild_docker() {
    local cnt="$(docker images -q ${cf_builder_img_name}:${cf_builder_img_tag} | wc -l)"
    if (( cnt != 0 )); then
        if [[ ${FLAGS_rebuild_docker_img} -ne ${FLAGS_TRUE} ]]; then
            return 1
        fi
    fi
    return 0
}

function build_image() {
    docker build -f Dockerfile.builder \
           --target ${cf_builder_img_target} \
           -t ${cf_builder_img_name}:${cf_builder_img_tag} \
           ${PWD} --build-arg UID=`id -u`
}

function run_cf_builder() {
    echo "args taken: " "$@"
    set -x

    local mount_volumes=()
    mount_volumes+=("-v" "${android_src_host_dir}:${android_src_mnt_dir}")
    if [[ ${FLAGS_share_gitconfig} == ${FLAGS_TRUE} ]]; then
        mount_volumes+=("-v" "$HOME/.gitconfig:/home/vsoc-01/.gitconfig")
    fi

    docker run --name cf_builder --privileged --rm \
           "${mount_volumes[@]}" "$@" \
           -it ${cf_builder_img_name}:${cf_builder_img_tag} \
           /bin/bash
}


# main routine starts frome here
if [ ${FLAGS_help} -eq ${FLAGS_TRUE} ]; then
    exit 0
fi

# when needed, create the image first
if is_rebuild_docker; then
    build_image
else
    echo "not building docker img"
fi

run_cf_builder "${arg_list[@]}"
