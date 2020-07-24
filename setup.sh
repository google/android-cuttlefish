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
  echo "       -n | --name jellyfish  : override default name"
  echo "                              : for backward compat, [NAME] will override this"
  echo "       -f | --foreground      : run the container in foreground"
  echo "                              : otherwise, the container is created as a daemon"
  echo "       -x | --with_host_x     : run the container in foreground and"
  echo "                              : share X of the docker host"
  echo "       -m | --share_home dir1 : share subdirectories of the host user's home"
  echo "                              : -m dir1 -m dir2 -m dir3 for multiple directories"
  echo "                              : dir1 should be an absolute path or relative path"
  echo "                              : to $HOME. $HOME itself is not allowed."
  echo "        -h | --help           : print this help message"
  echo "        The optional [NAME] will override -n option for backward compatibility"
}

function help_on_sourcing {
  echo "Create a cuttlefish container:"
  help_on_container_create

  echo "To list existing Cuttlefish containers:"
  echo "   cvd_docker_list"

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

function cvd_docker_create {
  local name=""
  local foreground="false"
  local with_host_x="false"
  local need_help="false"
  local share_home="false"
  local -a shared_home_subdirs=()

  # n | --name=cuttlefish | --name jellyfish
  # f | --foreground
  # x | --with_host_x
  # m | --share_home dir1
  # h | --help

  local params
  params=$(getopt -o 'n:m:sxh' -l 'name:,share_home:,singleshot,with_host_x,help' --name "$0" -- "$@") || return
  eval set -- "${params}"
  unset params
  while true; do
    case "$1" in
    -n|--name)
      name=$2
      shift 2
      ;;
    -m|--share_home)
      share_home="true"
      shared_home_subdirs+=("$2")
      echo "ADD $2 TO DIRS"
      shift 2
      ;;
    -f|--foreground)
      foreground="true"
      shift
      ;;
    -x|--with_host_x)
      with_host_x="true"
      foreground="true"
      shift
      ;;
   -h|--help)
      need_help="true"
      shift
      ;;
    --)
      shift
      break
      ;;
    *)
      echo "Not implemented: $1" >&2
      need_help="true"
      break
      ;;
    esac
  done

  if [[ "${need_help}" == "true" ]]; then
    help_on_container_create
    return
  fi

  local -a _rest=($@)
  [[ -z ${name} ]] && name="${_rest[0]}"
  unset _rest

  local name="$(cvd_get_id $name)"
  local container="$(cvd_exists $name)"

  local -a home_volume=()
  if [[ -z "${container}" ]]; then
    echo "Container ${name} does not exist.";
    echo "Starting container ${name} from image cuttlefish.";

    # If ANDROID_BUILD_TOP is set, we assume that the entire Android
    # build environment is correctly configured.  We further assume
    # that there is a valid $ANDROID_HOST_OUT/cvd-host_package.tar.gz
    # and a set of Android images under $ANDROID_PRODUCT_OUT/*.img.

    if [[ -v ANDROID_BUILD_TOP ]]; then
      local home="$(mktemp -d)"
      echo "Detected Android build environment.  Setting up in ${home}."
      tar xz -C "${home}" -f "${ANDROID_HOST_OUT}"/cvd-host_package.tar.gz
      for f in "${ANDROID_PRODUCT_OUT}"/*.img; do
        home_volume+=("-v ${f}:/home/vsoc-01/$(basename ${f}):rw")
      done
      home_volume+=("-v ${home}:/home/vsoc-01:rw")
    fi

    if [[ $share_home == "true" ]]; then
      local user_home=$(realpath $HOME)
      for sub in "${shared_home_subdirs[@]}"; do
        local subdir=${sub}
        if ! is_absolute_path ${subdir}; then
          subdir=${user_home}/${subdir}
        fi
        # mount /home/user/X to /home/vsoc-01/X
        local dstdir=${subdir#$user_home/}
        home_volume+=("-v ${subdir}:/home/vsoc-01/${dstdir}:rw")
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
           -v /sys/fs/cgroup:/sys/fs/cgroup:ro ${home_volume[@]} \
           cuttlefish

    __gen_funcs ${name}

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

