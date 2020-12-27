if [ "${BASH_SOURCE[0]}" -ef "$0" ]; then
	echo "source this script, do not execute it!"
	exit 1
fi

source "$(dirname ${BASH_SOURCE[0]})"/expose-port.sh

# set -o errexit
# set -x

function cvd_get_id {
	echo "${1:-cuttlefish}"
}

function cvd_container_exists {
	local name="$(cvd_get_id $1)"
	[[ $(docker ps --filter "name=^/${name}$" --format '{{.Names}}') == "${name}" ]] && echo "${name}";
}

function cvd_container_running {
	local name="$(cvd_get_id $1)"
	[[ $(docker inspect -f "{{.State.Running}}" ${name}) == 'true' ]] && echo "${name}"
}

function cvd_get_ip {
	local name="$(cvd_container_exists $1)"
	[[ -n "${name}" ]] && \
		echo $(docker inspect --format='{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' "${name}")
}

function cvd_get_instance_id {
    # $1 could be hash code or name for the container
    echo "$(docker inspect -f '{{- printf "%s" .Config.Labels.cf_instance}}' "$1")"
}

function cvd_get_vsock_guest_cid {
    # $1 could be hash code or name for the container
    echo "$(docker inspect -f '{{- printf "%s" .Config.Labels.vsock_guest_cid}}' "$1")"
}

function cvd_get_n_cf_instances {
    # $1 could be hash code or name for the container
    echo "$(docker inspect -f '{{- printf "%s" .Config.Labels.n_cf_instances}}' "$1")"
}

function cvd_allocate_instance_id {
	local -a ids=()
	for instance in $(docker ps -aq --filter="ancestor=cuttlefish"); do
		local id;
		id="$(cvd_get_instance_id "${instance}")"
		ids+=("${id}")
	done
	local sorted;
	IFS=$'\n' sorted=($(sort -n <<<"${ids[*]}")); unset IFS
	local prev=0
	for id in ${sorted[@]}; do
		if [[ "${prev}" -lt "${id}" ]]; then
			break;
		fi
		prev="$((id+1))"
	done
	echo "${prev}"
}

function cvd_docker_list {
	docker ps -a --filter="ancestor=cuttlefish"
}

function help_on_container_create {
	echo "   cvd_docker_create <options> [NAME] # NAME is by default cuttlefish"
	echo "     Options:"
	echo "       -s | --singleshot                : run the container, log in once, then delete it on logout"
	echo "                                        : otherwise, the container is created as a daemon"
	echo "       -x | --with_host_x               : run the container in singleshot and"
	echo "                                        : share X of the docker host"
	echo "       -m | --share_dir dir1:dir2       : mount a host directory dir1 at dir2 of docker container"
	echo "                                        : dir1 should be an absolute path or relative path to $PWD"
	echo "                                        : dir2 should be an absolute path or relative path to /home/vsoc-01/"
	echo "                                        : $HOME is not allowed as dir1"
	echo "                                        : /home/vsoc-01 is not allowed as dir2"
	echo "                                        : For multiple mounts, use multiple -m options per pair"
	echo "       -n | --n_cf_instances            : maximum number of cuttlefish instances inside a container"
	echo "       -A[/path] | --android[=/path]    : mount Android images from path (defaults to \$ANDROID_PRODUCT_OUT);"
	echo "                                        : requires -C to also be specified"
	echo "                                        : (Optional path argument must follow short-option name without intervening space;"
	echo "                                        :  it must follow long-option name followed by an '=' without intervening space)"
	echo "       -C[/path] | --cuttlefish[=/path] : mount Cuttlefish host image from path"
    echo "                                        : by default, \$ANDROID_PRODUCT_OUT is used to determine the location of the host pkg"
	echo "                                        : (Optional path argument must follow short-option name without intervening space;"
	echo "                                        :  it must follow long-option name followed by an '=' without intervening space)"
	echo "       -v | --vsock_guest_cid           : facilitate the new vsock_guest_cid option"
	echo "       -h | --help                      : print this help message"
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
	echo "Delete all containers:"
	echo "    cvd_docker_rm_all"
}

function is_absolute_path {
	local sub=$1
	if [[ -n ${sub:0:1} ]] && [[ "${sub:0:1}" == "/" ]]; then
		return 0
	fi
	return 1
}

singleshot="false"

function cvd_docker_create {
  local name=""
  local android=""
  local cuttlefish=""
  local with_host_x="false"
  local need_help="false"
  local share_dir="false"
  local -a shared_dir_pairs=()
  local vsock_guest_cid="false"
  local n_cf_instances=1

  # m | --share_dir dir1:dir2
  # n | --n_cf_instances
  # A | --android[=/PATH]
  # C | --cuttlefish[=/PATH to host package file]
  # s | --singleshot
  # x | --with_host_x
  # v | --vsock_guest_cid
  # h | --help

  singleshot="false" # could've been updated to "true" by previous cvd_docker_create

  local params
  if params=$(getopt -o 'm:n:A::C::svxh' -l 'share_dir:,n_cf_instances:,android::,cuttlefish::,singleshot,vsock_guest_cid,with_host_x,help' --name "$0" -- "$@"); then
	  eval set -- "${params}"
	  while true; do
		  case "$1" in
			  -m|--share_dir)
				  share_dir="true"
				  shared_dir_pairs+=("$2")
				  for sub in "${shared_dir_pairs[@]}"; do
					  if ! echo ${sub} | egrep ":" > /dev/null; then
						  echo "Argument ${sub} to $1 is ill-formated, should be host_dir:mnt_dir" 1>&2
						  need_help="true"
					  fi
				  done
				  shift 2
				  ;;
              -n|--n_cf_instances)
				  n_cf_instances=$2
				  shift 2
				  ;;
			  -A|--android)
				  android=$2
				  if [[ -z "${android}" ]]; then
					  if [[ -v ANDROID_PRODUCT_OUT ]]; then
						  android="${ANDROID_PRODUCT_OUT}"
						  echo "Defaulting Android path to ${android}"
						  if [[ ! -d "${ANDROID_PRODUCT_OUT}" ]]; then
							  echo "Directory ANDROID_PRODUCT_OUT=${ANDROID_PRODUCT_OUT} does not exist, can't use as default." 1>&2
							  need_help="true"
						  fi
					  fi
				  fi
				  if [[ ! -r "${android}" || ! -d "${android}" ]]; then
					  echo "Directory \"${android}\" does not exist." 1>&2
					  need_help="true"
				  fi
				  shift 2
				  ;;
			  -C|--cuttlefish)
				  cuttlefish=$2
				  if [[ -z "${cuttlefish}" ]]; then
					  if [[ -v ANDROID_HOST_OUT ]]; then
                          local new_host_out="$(echo $ANDROID_HOST_OUT  | sed 's/out\/host/out\/soong\/host/g')"
                          local new_cuttlefish="${new_host_out}/cvd-host_package.tar.gz"
                          cuttlefish="${new_cuttlefish}"
                          if [[ ! -r "${cuttlefish}" ]]; then
                              cuttlefish="${ANDROID_HOST_OUT}/cvd-host_package.tar.gz"
                          fi
						  echo "Defaulting Cuttlefish path to ${cuttlefish}"
						  if [[ ! -r "${cuttlefish}" ]]; then
							  echo "File ${cuttlefish} does not exist, can't use as default." 1>&2
							  need_help="true"
						  fi
					  fi
				  fi
				  if [[ ! -r "${cuttlefish}" || ! -f "${cuttlefish}" ]]; then
					  echo "File \"${cuttlefish}\" does not exist." 1>&2
					  need_help="true"
				  fi
				  shift 2
				  ;;
			    -s|--singleshot)
				  singleshot="true"
				  shift
				  ;;
			    -x|--with_host_x)
				  if [[ -n "${DISPLAY}" ]]; then
				    with_host_x="true"
				  else
				    echo "Can't use host's X: DISPLAY is not set." 1>&2
				    need_help="true"
				  fi
				  shift
				  ;;
			    -v|--vsock_guest_cid)
				  vsock_guest_cid="true"
				  shift 1
				  ;;
			    -h|--help)
				  need_help="true"
				  shift
				  ;;
			    --)
				  shift
				  break
				  ;;
		    esac
	    done
    else
	    need_help="true"
    fi

    if [[ -n "${android}" && -z "${cuttlefish}" ]]; then
	    echo "Option -A/--android requires option -C/--cuttlefish" 1>&2
	    need_help="true"
    fi

    if [[ "${need_help}" == "true" ]]; then
	    help_on_container_create
	    return
    fi

    # for backward compatibility:
    local -a _rest=($@)
    name="${_rest[0]}"
    unset _rest

    local name="$(cvd_get_id $name)"
    local container="$(cvd_container_exists $name)"

	local -a volumes=("-v $(pwd)/download-aosp.sh:/home/vsoc-01/download-aosp.sh:ro")
    if [[ -z "${container}" ]]; then
	    echo "Container ${name} does not exist.";

        local cf_instance=$(cvd_allocate_instance_id)
        if [ "${cf_instance}" -gt 7 ]; then
                echo "Limit is maximum 8 Cuttlefish instances."
                return
        fi

	    if [[ -f "${cuttlefish}" ]]; then
		    local home="$(mktemp -d)"
		    echo "Setting up Cuttlefish host image from ${cuttlefish} in ${home}."
		    tar xz -C "${home}" -f "${cuttlefish}"
	    fi
	    if [[ -d "${android}" ]]; then
		    echo "Setting up Android images from ${android} in ${home}."
		    if [[ $(compgen -G "${android}"/*.img) != "${android}/*.img" ]]; then
				for f in "${android}"/*.img; do
					volumes+=("-v ${f}:/home/vsoc-01/$(basename ${f}):rw")
				done
		    else
			    echo "WARNING: No Android images in ${android}."
		    fi
            if [ -f "${android}/bootloader" ]; then
                volumes+=("-v ${android}/bootloader:/home/vsoc-01/bootloader:rw")
            fi
	    fi
	    if [[ -f "${cuttlefish}" || -d "${android}" ]]; then
		    volumes+=("-v ${home}:/home/vsoc-01:rw")
	    fi

        if [[ $share_dir == "true" ]]; then
            # mount host_dir to guest_dir
            # if host_dir is symlink, use the symlink path to calculate guest_dir
            # however, mount the host actual target directory to the guest_dir
            local host_pwd=$(realpath -s "$PWD")
            local guest_home="/home/vsoc-01"
            for sub in "${shared_dir_pairs[@]}"; do
                local host_dir="$(echo $sub | cut -d ':' -f 1)"
                local guest_dir="$(echo $sub | cut -d ':' -f 2)"
                if ! is_absolute_path ${host_dir}; then
                    host_dir="${host_pwd}/${subdir}"
                fi
                if ! is_absolute_path ${guest_dir}; then
                    guest_dir="${guest_home}/${guest_dir}"
                fi
                host_dir=$(realpath ${host_dir}) # resolve symbolic link only on the host side
                volumes+=("-v ${host_dir}:${guest_dir}")
            done
        fi

	    local -a as_host_x=()
	    if [[ "$with_host_x" == "true" ]]; then
		    as_host_x+=("-e DISPLAY=$DISPLAY")
		    as_host_x+=("-v /tmp/.X11-unix:/tmp/.X11-unix")
	    fi

        echo "Starting container ${name} (id ${cf_instance}) from image cuttlefish.";
	    docker run -d ${as_host_x[@]} \
		        --name "${name}" -h "${name}" \
                -l "cf_instance=${cf_instance}" \
                -l "n_cf_instances=${n_cf_instances}" \
                -l "vsock_guest_cid=${vsock_guest_cid}" \
		        --privileged \
		        -v /sys/fs/cgroup:/sys/fs/cgroup:ro ${volumes[@]} \
		        cuttlefish

	    echo "Waiting for ${name} to boot."
	    while true; do
		    if [[ -z "$(cvd_container_exists ${name})" ]]; then
			    echo "Container ${name}  does not exist yet.  Sleep 1 second"
			    sleep 1
			    continue
		    fi
		    if [[ -z "$(cvd_container_running ${name})" ]]; then
			    echo "Container ${name} is not running yet.  Sleep 1 second"
			    sleep 1
			    continue
		    fi
		    break
	    done
	    echo "Done waiting for ${name} to boot."

	    __gen_funcs ${name}
	    __gen_publish_funcs ${name}
        # define and export ip_${name} for the ip address
        local ip_addr_var_name="ip_${name}"
        declare ${ip_addr_var_name}="$(cvd_get_ip "${name}")"
        export ${ip_addr_var_name}

	    if [[ "$singleshot" == "true" ]]; then
		    cvd_login_${name}
		    cvd_docker_rm ${name}
		    return
	    fi

	    help_on_container_start ${name}
	    echo
	    help_on_sourcing

    else
	    echo "Container ${name} exists";
	    if [[ -z "$(cvd_container_running ${name})" ]]; then
		    echo "Container ${name} is not running.";
	    else
		    echo "Container ${name} is already running.";
		    __gen_funcs ${name}
		    __gen_publish_funcs ${name}
		    help_on_container_start ${name}
		    echo
		    help_on_sourcing
	    fi
    fi
}

function cvd_docker_rm {
	local name=${1:-cuttlefish}
	while [ ! -z "${name}" ]; do
        local ip_addr_var_name="ip_${name}"
        unset ${ip_addr_var_name}

		if [ -n "$(docker ps -q -a -f name=${name})" ]; then
			homedir=$(cvd_gethome_${name})
			echo "Deleting container ${name}."
			docker rm -f ${name}
			echo "Cleaning up homedir ${homedir}."
			rm -rf ${homedir}
			echo "Closing socat if any"
			$(__gen_unpublish_func_name ${name})
			unset $(__gen_unpublish_func_name ${name})
			unset $(__gen_publish_func_name ${name})
			unset -f $(__gen_start_func_name ${name})
			unset -f $(__gen_login_func_name ${name})
			unset -f $(__gen_stop_func_name ${name})
		else
			echo "Nothing to stop: container ${name} does not exist."
		fi
		shift
		name=$1
	done
}

function cvd_docker_rm_all {
	for c in $(docker ps -qa --filter="ancestor=cuttlefish"); do
		local name=$(docker inspect -f '{{.Name}}' ${c})
		if [ "${name:0:1}" == "/" ]; then
			# slice off the leading slash
			name=("${name:1}")
		fi
		cvd_docker_rm "${name}"
	done
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

function __gen_gethome_func_name {
	local name=$1
	echo -n "cvd_gethome_${name}"
}

# $1 = container name; must not be empty
function __gen_funcs {
	local name=$1
	local instance_id=$(cvd_get_instance_id ${name})
	local vcid_opt="--base_instance_num=${instance_id}"
	local login_func
	local start_func
	local stop_func
	local gethome_func

	if [[ "$(cvd_get_vsock_guest_cid ${name})" == "true" ]]; then
	  local cid=$((instance_id + 2))
	  vcid_opt+=("--vsock_guest_cid=${cid}")
	fi

read -r -d '' login_func <<EOF
function $(__gen_login_func_name ${name}) {
  local _cmd="/bin/bash"
  if [[ -n "\$@" ]]; then
	_cmd="\$@"
  fi
  docker exec -it --user vsoc-01 "${name}" \${_cmd}
}
EOF

read -r -d '' start_func <<EOF
function $(__gen_start_func_name ${name}) {
  $(__gen_login_func_name ${name}) ./bin/launch_cvd "${vcid_opt}" "\$@"
}
EOF

read -r -d '' stop_func <<EOF
function $(__gen_stop_func_name ${name}) {
  docker exec -it --user vsoc-01 "${name}" ./bin/stop_cvd;
}
EOF

read -r -d '' gethome_func <<EOF
function $(__gen_gethome_func_name ${name}) {
  docker inspect -f '{{range \$mount:=.Mounts}}{{if and (eq .Destination "/home/vsoc-01") (eq .Type "bind")}}{{- printf "%q" \$mount.Source}}{{end}}{{end}}' "${name}" | sed 's/"//g'
}
EOF

	eval "${login_func}"
	eval "${start_func}"
	eval "${stop_func}"
	eval "${gethome_func}"
	eval "export ip_${name}=$(cvd_get_ip $(cvd_get_id ${name}))"

	if [[ "$singleshot" == "true" ]]; then
	  return
	fi

	echo "To log into container ${name} without starting Android, call $(__gen_login_func_name ${name})"
	echo "To start Android in container ${name}, call $(__gen_start_func_name ${name})"
	echo "To stop Android in container ${name}, call $(__gen_stop_func_name ${name})"
	echo "To get the home directory of container ${name}, call $(__gen_gethome_func_name ${name})"
}

function __gen_publish_func_name {
    local name=$1
    echo -n "cvd_publish_${name}"
}
function __gen_unpublish_func_name {
    local name=$1
    echo -n "cvd_unpublish_${name}"
}

function __gen_publish_funcs {
    local name=$1
    local sz=$(cvd_get_n_cf_instances ${name})
    local instance_id=$(cvd_get_instance_id ${name})
    local host_offset_default=$((instance_id * sz))
    local host_offset=${2:-$host_offset_default}
    local guest_ip=$(cvd_get_ip ${name})
    local guest_offset=0
    local publish_func
    local unpublish_func

    if [[ "$(cvd_get_vsock_guest_cid ${name})" == "false" ]]; then
        # use instance_num
        guest_offset=${host_offset}
    fi

    #
    # If not overriden, host offset port will be base + instance_id * sz
    # that is, 6444(vnc) + instance_id * no. of MAX instances in a container
    # Host port offset can be overriden when cvd_publish_${name} is invoked
    #
    # guest port offset is independent from host offset
    # if vsock_guest_cid is given, the guest port offset is set to 0
    # if not given, the guest ports depend on the --base_instance_num
    # here, we assume that it is strongly recommended to be the same as instance_id
    # Thus, the guest port offset is automatically adjusted by that assumption
    #
read -r -d '' publish_func <<EOF
function $(__gen_publish_func_name ${name}) {
  local h_offset=\${1:-$host_offset}
  port_expose ${name} ${guest_ip} ${sz} \${h_offset} ${guest_offset}
}
EOF

read -r -d '' unpublish_func <<EOF
function $(__gen_unpublish_func_name ${name}) {
  port_close $(cvd_get_ip ${name})
}
EOF

    eval "${publish_func}"
    eval "${unpublish_func}"
    if [[ "$singleshot" == "true" ]]; then
        return
    fi
    echo "To export ports to container ${name}, $(__gen_publish_func_name ${name}) [host offset]"
    echo "      e.g. $(__gen_publish_func_name ${name}) 0, to make the host ports same as default cuttlefish ports"
    echo "      e.g. $(__gen_publish_func_name ${name})    to automatically find host ports"
    echo "To undo the exported ports for container ${name}, $(__gen_unpublish_func_name ${name})"
}

help_on_sourcing

function cvd_clean_autogens() {
	for f in $(compgen -A function cvd_login_); do
		unset -f ${f}
	done
	for f in $(compgen -A function cvd_start_); do
		unset -f ${f}
	done
	for f in $(compgen -A function cvd_stop_); do
		unset -f ${f}
	done
	for f in $(compgen -A function cvd_gethome_); do
		unset -f ${f}
	done
}
# If containers were removed in different shell sessions, this session might
# have stale instances of the auto-generated functions.
cvd_clean_autogens
unset -f cvd_clean_autogens

for cf in $(docker ps -q -a --filter="ancestor=cuttlefish" --format "table {{.Names}}" | tail -n+2); do
	__gen_funcs "${cf}"
    __gen_publish_funcs "${cf}"
done
