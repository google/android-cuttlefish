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

  echo "Log into container ${name} with ssh:"
  echo "    ssh vsoc-01@ip_${name}"
  echo "Log into container ${name} with docker:"
  echo "    docker exec -it --user vsoc-01 $(cvd_get_id ${name}) /bin/bash"
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
  local OPTIND
  local op
  local val
  local OPTARG

  local name_=""
  local foreground="false"
  local with_host_x="false"
  local need_help="false"
  local share_home="false"
  local -a shared_home_subdirs=()

  while getopts ":m:n:-:hfx" op; do
    # n | --name=cuttlefish | --name jellyfish
    # f | --foreground
    # x | --with_host_x
    # m | --share_home dir1
    # h | --help
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
            echo $op
            echo $OPTARG
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
      m | share_home )
        share_home="true"
        shared_home_subdirs+=("${OPTARG}")
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

  # for backward compatibility:
  [[ -n ${!OPTIND} ]] && name_="${!OPTIND}"

  local name="$(cvd_get_id $name_)"
  local container="$(cvd_exists $name_)"

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
    unset $(__gen_stop_func_name ${name})
  else
    echo "Nothing to stop: container ${name} does not exist."
  fi
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

  local start_func
  local stop_func

  read -r -d '' start_func <<EOF
function $(__gen_start_func_name ${name}) {
  ssh \
    -L8443:localhost:8443 \
    -L6520:localhost:6520 \
    -L6444:localhost:6444 \
    -L15550:localhost:15550 -L15551:localhost:15551 \
    vsoc-01@$(cvd_get_ip ${name}) -- ./bin/launch_cvd "\$@";
#  docker exec -it --user vsoc-01 "${name}" ./bin/launch_cvd "$@";
}
EOF

  read -r -d '' stop_func <<EOF
function $(__gen_stop_func_name ${name}) {
  docker exec -it --user vsoc-01 "${name}" ./bin/stop_cvd;
}
EOF

  eval "${start_func}"
  eval "${stop_func}"
  eval "export ip_${name}=$(cvd_get_ip $(cvd_get_id ${name}))"

  echo "To start ${name}, call $(__gen_start_func_name ${name})"
  echo "To stop ${name}, call $(__gen_stop_func_name ${name})"
}

help_on_sourcing

for cf in $(docker ps -q -a --filter="ancestor=cuttlefish" --format "table {{.Names}}" | tail -n+2); do
  __gen_funcs "${cf}"
done

