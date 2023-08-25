#!/bin/bash

# for now, the script will build docker image as needed
# and gives a bash shell inside the container, so that
# the user can run source, lunch & m commands to build
#

SCRIPT_DIR=$(dirname ${BASH_SOURCE[0]})

# to print multi-lined helper for shflags
function multiline_helper {
    local aligner="$(printf '%*s' "$1")"
    local -n msgs=$2
    echo ${msgs[0]}
    for msg in "${msgs[@]:1}"; do
        echo "${aligner}""$msg"
    done
}

function get_default_lunch_target {
    local def_lunch_target=""
    case "$(uname -m)" in
        x86_64|i386|x86|amd64) def_lunch_target="aosp_cf_x86_64_phone-trunk_staging-userdebug" ;;
        aarch64|arm64) def_lunch_target="aosp_cf_arm64_phone-userdebug" ;;
        *)
            >&2 echo "unsupported architecture $(uname -m)"
            exit 10
            ;;
    esac
    echo $def_lunch_target
}

script_name=$(basename $0)

default_lunch_target="$LUNCH_TARGET"
if [[ -z "$LUNCH_TARGET" ]]; then
    default_lunch_target="$(get_default_lunch_target)"
fi

default_repo_src="$REPO_SRC"
if [[ -z "$REPO_SRC" ]]; then
    default_repo_src="https://android.googlesource.com/platform/manifest"
fi

# android_src_mnt helper message
android_src_mnt_helper=("src_dir:mnt_ptr | src_dir | :mnt_ptr | <an empty string>")
android_src_mnt_helper+=("  src_dir on the host will be mounted mnt_ptrer on the docker container")
android_src_mnt_helper+=("    e.g. /home/$USER/aosp03:/home/vsoc-01/aosp")
android_src_mnt_helper+=("    This will mount /home/$USER/aosp03 to /home/vsoc-01/aosp in the container")
android_src_mnt_helper+=("  if mnt_ptr is missing, it is the src_dir with $HOME if any being replaced with /home/vsoc-01")
android_src_mnt_helper+=("  if src_dir is missing, it should be given in the environment variable ANDROID_BUILD_TOP.")

# op_mode help message
supported_op_modes=("bash | host | guest | image_build | intrinsic")
op_mode_helper=("$supported_op_modes")
op_mode_helper+=("  determine the mode in which $script_name operates.")
op_mode_helper+=("    bash mode:")
op_mode_helper+=("      gives a bash shell inside the docker container.")
op_mode_helper+=("      e.g. $0 <options> --op_mode=bash -- optional bash args if any")
op_mode_helper+=("    host mode:")
op_mode_helper+=("      mount the host script to the corresponding location.")
op_mode_helper+=("        i.e. host script path --> realpath -s --> replace $HOME")
op_mode_helper+=("      and run it in the mnt_ptr of the src_dir.")
op_mode_helper+=("        see --android_src_mnt.")
op_mode_helper+=("    guest mode:")
op_mode_helper+=("      run guest commands with optional arguments followed by --")
op_mode_helper+=("      e.g. $0 <options> --op_mode=guest -- ls -l /home")
op_mode_helper+=("    image_build mode:")
op_mode_helper+=("      only (re)build the docker image.")
op_mode_helper+=("    intrinsic mode:")
op_mode_helper+=("      regards args as an intrinsic command that could be:")
op_mode_helper+=("        init, sync, and build")
op_mode_helper+=("      this is a showcase operation mode.")
op_mode_helper+=("      ${script_name} reads REPO_SRC, LUNCH_TARGET.")
op_mode_helper+=("      Following that, it does repo init, sync, or m -j N in the mnt_ptr.")
op_mode_helper+=("        see --android_src_mnt")
op_mode_helper+=("      if REPO_SRC and LUNCH_TARGET is not set, the defaults are:")
op_mode_helper+=("        REPO_SRC=$default_repo_src")
op_mode_helper+=("        LUNCH_TARGET=$default_lunch_target")

# docker_run_opts help message
docker_run_opts_helper=("passing arguments to docker run, not the guest/host scripts or bash")
docker_run_opts_helper+=("  comma separated, and no space is allowed.")
docker_run_opts_helper+=("  e.g. --rm,--privileged,-eENV,-vSRC:DIR,--name=MYNAME")

source "${SCRIPT_DIR}/shflags"

DEFINE_boolean rebuild_docker_img false "Rebuild cuttlefish-android-builder image" "" "f"
DEFINE_boolean share_gitconfig true "Allow the docker container to use the host gitconfig" "g"
DEFINE_boolean rm true "Pass --rm to docker run" "r"
DEFINE_string android_src_mnt \
              "" \
              "$(multiline_helper 25 android_src_mnt_helper)" "a"
DEFINE_string op_mode \
              "bash" \
              "$(multiline_helper 17 op_mode_helper)" "m"
DEFINE_string lunch_target \
              "$default_lunch_target" \
              "default lunch target used by the --op_mode=intrinsic only" "l"
DEFINE_string repo_src \
              "$default_repo_src" \
              "default Android Repo source used by the --op_mode=intrinsic only" "s"
DEFINE_integer n_parallel "0" \
               "default N for m/repo sync -j N. we use /proc/cpuinfo if 0" "N"
DEFINE_string instance_name \
              "cf_builder" \
              "default name of the docker container that builds Android" "n"
DEFINE_string docker_run_opts "" \
              "$(multiline_helper 25 docker_run_opts_helper)" "d"

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
# "${arg_list[@]}" is ready to be used after the following call
parse_cmds

if [ ${FLAGS_help} -eq ${FLAGS_TRUE} ]; then
    exit 0
fi

# global util function
# 1: src dir, either symlink or dir
# 2: substring to be gone
# 3: substring to replace $2
# echo the absolute path with $2 being replaced $3
function calc_dst_dir() {
    local srcdir="$(realpath -s $1)"
    local to_go=$(echo $2 | sed -e 's/\//\\\//g')
    local to_come=$(echo $3 | sed -e 's/\//\\\//g')
    echo ${srcdir} | sed -e "s/$to_go/$to_come/g"
}

# pick up src dir and its mnt point
android_src_host_dir=""
android_src_mnt_dir=""

# util func used by calc_mnt_point
function no_src_dir2mount() {
    >&2 echo "Error: --android_src_mnt should be given and indicate at least source directory. try:"
    >&2 echo "       $0 -h"
    >&2 echo "       Alternatively, set ANDROID_BUILD_TOP to the root of the Android source directory."
    exit 10
}

#util func used by calc_mnt_point
function default_mnt_dir() {
    echo "$(calc_dst_dir $1 $HOME /home/vsoc-01)"
}

# output: android_src_{host,mnt}_dir
# input: FLAGS_android_src_mnt and/or ANDROID_BUILD_TOP env variable
function calc_mnt_point() {
    local arg=${FLAGS_android_src_mnt}
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
        tk1=$(default_mnt_dir "$(realpath -s ${tk0})")
    fi
    android_src_mnt_dir="${tk1}"

    if [[ "${android_src_mnt_dir}" == "/home/vsoc-01" ]]; then
        >&2 echo "Error: We don't allow the source to be mounted exactly at /home/vsoc-01"
        >&2 echo "       Please consider to mount it at a subdirectory of /home/vsoc-01"
        exit 9
    fi
}

# util to parse docker run options
# 1: string to parse
# 2: reference to array
# 3: optional delimiter
#
# don't pass an empty ${FLAGS_string}
function string_to_array() {
    local what2parse="$1"
    local -n result=$2
    local delim=${3:-','}
    local IFS=
    while IFS=',' read -ra opts; do
        for op in "${opts[@]}"; do
            result+=("$op")
        done
    done <<< "$what2parse"
}

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

# Output
cf_builder_img_name="cuttlefish-android-builder"
cf_builder_img_tag="latest"

# see Dockerfile.builder, the target name must match
cf_builder_img_target="cuttlefish-android-builder"

function build_image() {
    pushd ./android-builder-docker > /dev/null 2>&1
    if cat Dockerfile.builder | egrep "AS[[:space:]]+$cf_builder_img_target" \
                                      > /dev/null 2>&1; then
        >&2 echo "The target name in Dockerfile.builder must match $cf_builder_img_target"
    fi
    docker build -f ${SCRIPT_DIR}/Dockerfile.builder \
           --target ${cf_builder_img_target} \
           -t ${cf_builder_img_name}:${cf_builder_img_tag} \
           ${SCRIPT_DIR} --build-arg UID=`id -u`
    popd > /dev/null 2>&1
    err_code=$?
    if (( err_code != 0)); then
        >&2 echo "image build failed"
        exit $err_code
    fi
}

function verify_intrinsic_mode() {
    if (( $# != 1 )); then
        >&2 echo "--op_mode=intrinsic takes only one args, one of these:"
        >&2 echo "  <$supported_op_modes>"
        exit 40
    fi
    case $1 in
        init)
        ;;
        sync)
        ;;
        build)
        ;;
        *)
            >&2 echo "unsupported value for --op_mode=intrinsic"
            exit 41
            ;;
    esac
    return 0 # return success
}

function run_cf_builder() {
    local -a parms=( "$@" )

    case "${FLAGS_op_mode}" in
        image_build)
            build_image
            return
            ;;
        *)
            ;; # keep going
    esac

    local -a mount_volumes=()
    calc_mnt_point
    mount_volumes+=("-v" "${android_src_host_dir}:${android_src_mnt_dir}")
    if [[ ${FLAGS_share_gitconfig} == ${FLAGS_TRUE} ]]; then
        mount_volumes+=("-v" "$HOME/.gitconfig:/home/vsoc-01/.gitconfig")
    fi

    local cmd_to_docker="/bin/bash"

    local -a env_variables=()
    export N_PARALLEL=${FLAGS_n_parallel}
    env_variables+=("-e" "N_PARALLEL")

    # a script change dir to $1, and run $2 "shift-2'ed $@"
    local chdirer="/home/vsoc-01/intrinsic_shells/bin/run.sh"

    case "${FLAGS_op_mode}" in
        intrinsic)
            env_variables+=("-e" "LUNCH_TARGET")
            env_variables+=("-e" "REPO_SRC")
            export LUNCH_TARGET="${FLAGS_lunch_target}"
            export REPO_SRC="${FLAGS_repo_src}"
            if ! verify_intrinsic_mode "${parms[@]}"; then
                exit 40
            fi
            local intrinsic_cmd="/home/vsoc-01/intrinsic_shells/bin/common_intrinsic.sh"
            cmd_to_docker="$intrinsic_cmd"
            parms=("${parms[@]}" "$android_src_mnt_dir")
            echo ${parms[@]}
            echo $cmd_to_docker
            ;;
        bash)
            ;;
        guest)
            cmd_to_docker=$chdirer
            parms=("$android_src_mnt_dir" "${parms[@]}")
            ;;
        host)
            local host_script_path="$(realpath -s ${parms[0]})"
            local host_script="$(realpath $host_script_path)"
            local guest_script="$(calc_dst_dir $host_script_path $HOME /home/vsoc-01)"
            mount_volumes+=("-v" "${host_script}:${guest_script}")
            parms=("$android_src_mnt_dir" "$guest_script" "${parms[@]:1}")
            cmd_to_docker=$chdirer
            ;;
        image_build)
            >&2 echo "control shouldn't reach here"
            exit 9
            ;;
        *)
            >&2 echo "unsupported --op_mode"
            exit 10
            ;;
    esac

    # when needed, create the image first
    if is_rebuild_docker; then
        build_image
    else
        echo "not building docker img"
    fi

    local -a opt_rm=()
    if [ ${FLAGS_rm} -eq ${FLAGS_TRUE} ]; then
        opt_rm+=("--rm")
    fi

    set -x
    local -a dck_usr_opts=()
    if [[ "${FLAGS_docker_run_opts}" != "" ]]; then
        string_to_array "${FLAGS_docker_run_opts}" dck_usr_opts ','
    fi
    docker run --name="${FLAGS_instance_name}" \
           --privileged "${opt_rm[@]}" "${env_variables[@]}" \
           "${mount_volumes[@]}" "${dck_usr_opts[@]}" \
           -it ${cf_builder_img_name}:${cf_builder_img_tag} \
           ${cmd_to_docker} "${parms[@]}" # all parameters are passed to cmd_to_docker
}

run_cf_builder "${arg_list[@]}"
