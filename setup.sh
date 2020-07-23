if [ "${BASH_SOURCE[0]}" -ef "$0" ]; then
        echo "source this script, do not execute it!"
        exit 1
fi

# set -o errexit
# set -x

function cvd_get_id {
  echo "${1:-cuttlefish}"
}

function cvd_exists {
  local name="$(cvd_get_id $1)"
  [[ $(docker ps --filter "name=^/${name}$" --format '{{.Names}}') == "${name}" ]] && echo "${name}";
}

function cvd_get_ip {
  local name="$(cvd_exists $1)"
  [[ -n "${name}" ]] && \
    echo $(docker inspect --format='{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' "${name}")
}

function cvd_docker_list {
  docker ps -a --filter="ancestor=cuttlefish"
}

function help_on_container_create {
  echo "   cvd_docker_create <options> [NAME] # by default names 'cuttlefish'"
  echo "     Options:"
  echo "       -n | --name jellyfish        : override default name"
  echo "                                    : for backward compat, [NAME] will override this"
  echo "       -f | --foreground            : run the container in foreground"
  echo "                                    : otherwise, the container is created as a daemon"
  echo "       -x | --with_host_x           : run the container in foreground and"
  echo "                                    : share X of the docker host"
  echo "       -a | --android_build_top dir : equivalent as setting ANDROID_BUILD_TOP"
  echo "                                    : if ANDROID_BUILD_TOP is already set, the option is ignored"
  echo "                                    : dir should be the level directory of android tree"
  echo "       -m | --share_dir dir1:dir2   : mount a host directory dir1 at dir2 of docker container"
  echo "                                    : dir1 should be an absolute path or relative path to $PWD"
  echo "                                    : dir2 should be an absolute path or relative path to /home/vsoc-01/"
  echo "                                    : $HOME is not allowed as dir1"
  echo "                                    : /home/vsoc-01 is not allowed as dir2"
  echo "                                    : For multiple mounts, use multiple -m options per pair"
  echo "       -h | --help                  : print this help message"
  echo "        The optional [NAME] will override -n option for backward compatibility"
}

function help_on_sourcing {
  echo "Create a cuttlefish container:"
  help_on_container_create
  echo ""
  echo "To list existing Cuttlefish containers:"
  echo "   cvd_docker_list"
  echo ""
  echo "Existing Cuttlefish containers:"
  cvd_docker_list
}

function help_on_container_start {
  local name=$1

  echo "Log into container ${name}: $(__gen_login_func_name ${name})"
#  echo "Log into container ${name} with ssh:"
#  echo "    ssh vsoc-01@\${ip_${name}"}
#  echo "Log into container ${name} with docker:"
#  echo "    docker exec -it --user vsoc-01 $(cvd_get_id ${name}) /bin/bash"
  echo "Start Cuttlefish: $(__gen_start_func_name ${name})"
  echo "Stop Cuttlefish: $(__gen_stop_func_name ${name})"
  echo "Delete container ${name}:"
  [[ "${name}" == 'cuttlefish' ]] && echo "    cvd_docker_rm"
  [[ "${name}" != 'cuttlefish' ]] && echo "    cvd_docker_rm ${name}"
}

function is_absolute_path {
  local sub=$1
  if [[ -n ${sub:0:1} ]] && [[ "${sub:0:1}" == "/" ]]; then
    return 0
  fi
  return 1
}

# set up android_prod_out_dir, android_host_out_dir,
# and android_build_top_dir
#
# based on the android_build_top_dir, the routine figures out
# the rest, using find and egrep
function setup_android_build_envs {
  local -n android_build_top_dir_=$1
  local -n android_host_out_dir_=$2
  local -n android_prod_out_dir_=$3
  android_build_top_dir_="$(realpath $4)"

  local host_pkg_file=
  if ! host_pkg_file="$(find "$android_build_top_dir_"/out/ -type f | egrep "/cvd-host_package\.tar\.gz$" | tail -1 2> /dev/null)"; then
      echo "failed to search ANDROID_HOST_OUT directory"
      echo "try to set it up manually"
      exit 1
  fi
  android_host_out_dir_="$(dirname $host_pkg_file)"
  android_host_out_dir_="$(realpath $android_host_out_dir_)"
  local boot_img_file=
  if ! boot_img_file="$(find "$android_build_top_dir_"/out/ -type f | egrep "/boot\.img$"  | tail -1 2> /dev/null)"; then
      echo "failed to search ANDROID_PRODUCT_OUT directory"
      echo "try to set it up manually"
      exit 1
  fi
  android_prod_out_dir_="$(dirname $boot_img_file)"
  android_prod_out_dir_="$(realpath $android_prod_out_dir_)"
}

function cvd_docker_create {
  local OPTIND
  local op
  local val
  local OPTARG

  local name_=""
  local foreground="false"
  local with_host_x="false"
  local need_help="false"
  local share_dir="false"
  local -a shared_dir_pairs=()
  local android_build_top="false"
  local to_build_top_dir="${ANDROID_BUILD_TOP}"

  while getopts ":m:a:n:-:hfx" op; do
    # n | --name=cuttlefish | --name jellyfish
    # f | --foreground
    # x | --with_host_x
    # m | --share_dir dir1:dir2
    # h | --help
    # a | --android_build_top dir
    if [[ $op == '-' ]]; then
      case "${OPTARG}" in
        *=* )
          val=${OPTARG#*=}
          op=${OPTARG%=$val}
          OPTARG=${val}
          ;;
        *)
          op=${OPTARG}
          val=${!OPTIND}
          if [[ -n $val ]] && [[ ${val:0:1} != '-' ]]; then
            OPTARG=${val}
            OPTIND=$(( OPTIND + 1 ))
          fi
          ;;
      esac
    fi
    case "$op" in
      n | name ) name_=${OPTARG}
        ;;
      f | foreground ) foreground="true"
        ;;
      x | with_host_x )
        with_host_x="true"
        foreground="true"
        ;;
      m | share_dir )
        share_dir="true"
        shared_dir_pairs+=("${OPTARG}")
        ;;
      a | android_build_top )
        if [[ -z ${ANDROID_BUILD_TOP} ]]; then
            android_build_top="true"
            to_build_top_dir="${OPTARG}"
        fi
        ;;
      h | help ) need_help="true"
        ;;
      ? ) need_help="true"
        ;;
    esac
  done

  if [[ "${need_help}" == "true" ]]; then
    help_on_container_create
    return
  fi

  local android_build_top_dir="${ANDROID_BUILD_TOP}"
  local android_host_out_dir="${ANDROID_HOST_OUT}"
  local android_prod_out_dir="${ANDROID_PRODUCT_OUT}"
  if [[ "${android_build_top}" == "true" ]]; then
      setup_android_build_envs \
          android_build_top_dir \
          android_host_out_dir \
          android_prod_out_dir \
          ${to_build_top_dir}
  fi

  # for backward compatibility:
  [[ -n ${!OPTIND} ]] && name_="${!OPTIND}"

  local name="$(cvd_get_id $name_)"
  local container="$(cvd_exists $name_)"

  local -a volumes=()
  if [[ -z "${container}" ]]; then
    echo "Container ${name} does not exist.";
    echo "Starting container ${name} from image cuttlefish.";

    # if ANDROID_BUILD_TOP is given either via an env variable or commandline option,
    # we will use the host package and images from there
    #
    if [[ -n "${android_build_top_dir}" ]]; then
      local home="$(mktemp -d)"
      echo "Detected Android build environment.  Setting up in ${home}."
      tar xz -C "${home}" -f "${android_host_out_dir}"/cvd-host_package.tar.gz
      for f in "${android_prod_out_dir}"/*.img; do
        volumes+=("-v ${f}:/home/vsoc-01/$(basename ${f}):rw")
      done
      volumes+=("-v ${home}:/home/vsoc-01:rw")
    fi

    if [[ $share_dir == "true" ]]; then
      local host_pwd="$(realpath "$PWD")"
      local guest_home="/home/vsoc-01"
      for sub in "${shared_dir_pairs[@]}"; do
        if ! echo ${sub} | egrep ":" > /dev/null; then
          echo "${sub} is ill-formated. should be host_dir:mnt_dir"
          echo "try $0 --help"
          exit 1
        fi
        local host_dir="$(echo $sub | cut -d ':' -f 1)"
        local guest_dir="$(echo $sub | cut -d ':' -f 2)"
        if ! is_absolute_path ${host_dir}; then
          host_dir="${host_pwd}/${subdir}"
        fi
        if ! is_absolute_path ${guest_dir}; then
          guest_dir="${guest_home}/${guest_dir}"
        fi
        volumes+=("-v ${host_dir}:${guest_dir}")
      done
    fi

    local -a as_host_x=()
    if [[ "$with_host_x" == "true" ]]; then
      as_host_x+=("-e DISPLAY=$DISPLAY")
      as_host_x+=("-v /tmp/.X11-unix:/tmp/.X11-unix")
    fi

    docker run -d ${as_host_x[@]} \
           --name "${name}" -h "${name}" \
           --privileged \
           -v /sys/fs/cgroup:/sys/fs/cgroup:ro ${volumes[@]} \
           cuttlefish

    __gen_funcs ${name}

    # define and export ip_${name} for the ip address
    local ip_addr_var_name="ip_${name}"
    declare ${ip_addr_var_name}="$(cvd_get_ip "${name}")"
    export ${ip_addr_var_name}
    if [[ "$foreground" == "true" ]]; then
        docker exec -it --user vsoc-01 ${name} /bin/bash
        cvd_docker_rm ${name}
        return
    fi

    help_on_container_start ${name}
    echo
    help_on_sourcing

  else
    echo "Container ${name} exists";
    if [ $(docker inspect -f "{{.State.Running}}" ${name}) != 'true' ]; then
      echo "Container ${name} is not running.";
    else
      echo "Container ${name} is already running.";
      __gen_funcs ${name}
      help_on_container_start ${name}
      echo
      help_on_sourcing
    fi
  fi
}

function cvd_docker_rm {
  local name=${1:-cuttlefish}
  local ip_addr_var_name="ip_${name}"
  unset ${ip_addr_var_name}

  if [ -n "$(docker ps -q -a -f name=${name})" ]; then
    homedir=$(docker inspect -f '{{range $mount:=.Mounts}}{{if and (eq .Destination "/home/vsoc-01") (eq .Type "bind")}}{{- printf "%q" $mount.Source}}{{end}}{{end}}' "${name}" | sed 's/"//g')
    echo "Deleting container ${name}."
    docker rm -f ${name}
    echo "Cleaning up homedir ${homedir}."
    rm -rf ${homedir}
    unset $(__gen_start_func_name ${name})
    unset $(__gen_login_func_name ${name})
    unset $(__gen_stop_func_name ${name})
  else
    echo "Nothing to stop: container ${name} does not exist."
  fi
}

function __gen_login_func_name {
  local name=$1
  echo -n "cvd_login_${name}"
}

function __gen_start_func_name {
  local name=$1
  echo -n "cvd_start_${name}"
}

function __gen_stop_func_name {
  local name=$1
  echo -n "cvd_stop_${name}"
}

# $1 = container name; must not be empty
function __gen_funcs {
  local name=$1

  local login_func
  local start_func
  local stop_func

  read -r -d '' login_func <<EOF
function $(__gen_login_func_name ${name}) {
  ssh \
    -L8443:localhost:8443 \
    -L6520:localhost:6520 \
    -L6444:localhost:6444 \
    -L15550:localhost:15550 -L15551:localhost:15551 \
    vsoc-01@$(cvd_get_ip ${name}) -- "\$@";
#  docker exec -it --user vsoc-01 "${name}" ./bin/launch_cvd "$@";
}
EOF

  read -r -d '' start_func <<EOF
function $(__gen_start_func_name ${name}) {
  $(__gen_login_func_name ${name}) ./bin/launch_cvd "\$@"
}
EOF

  read -r -d '' stop_func <<EOF
function $(__gen_stop_func_name ${name}) {
  docker exec -it --user vsoc-01 "${name}" ./bin/stop_cvd;
}
EOF

  eval "${login_func}"
  eval "${start_func}"
  eval "${stop_func}"
  eval "export ip_${name}=$(cvd_get_ip $(cvd_get_id ${name}))"

  echo "To log into container ${name} without starting Android, call $(__gen_login_func_name ${name})"
  echo "To start Android in container ${name}, call $(__gen_start_func_name ${name})"
  echo "To stop Android in container ${name}, call $(__gen_stop_func_name ${name})"
}

help_on_sourcing

for cf in $(docker ps -q -a --filter="ancestor=cuttlefish" --format "table {{.Names}}" | tail -n+2); do
  __gen_funcs "${cf}"
done

