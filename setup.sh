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

function help_on_sourcing {
  echo "Create a cuttlefish container:"
  echo "   cvd_docker_create # by default names 'cuttlefish'"
  echo "   cvd_docker_create name # name is 'name'"

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

function cvd_docker_create {
  local name="$(cvd_get_id $1)"
  local container="$(cvd_exists $1)"
  if [[ -z "${container}" ]]; then
    echo "Container ${name} does not exist.";
    echo "Starting container ${name} from image cuttlefish.";

    # If ANDROID_BUILD_TOP is set, we assume that the entire Android
    # build environment is correctly configured.  We further assume
    # that there is a valid $ANDROID_HOST_OUT/cvd-host_package.tar.gz
    # and a set of Android images under $ANDROID_PRODUCT_OUT/*.img.

    local -a home_volume=()

    if [[ -v ANDROID_BUILD_TOP ]]; then
      local home="$(mktemp -d)"
      echo "Detected Android build environment.  Setting up in ${home}."
      tar xz -C "${home}" -f "${ANDROID_HOST_OUT}"/cvd-host_package.tar.gz
      for f in "${ANDROID_PRODUCT_OUT}"/*.img; do
        home_volume+=("-v ${f}:/home/vsoc-01/$(basename ${f}):rw")
      done
      home_volume+=("-v ${home}:/home/vsoc-01:rw")
    fi

    docker run -d \
            --name "${name}" -h "${name}" \
            --privileged \
            -v /sys/fs/cgroup:/sys/fs/cgroup:ro \
            ${home_volume[@]} \
            cuttlefish

    __gen_funcs ${name}
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

