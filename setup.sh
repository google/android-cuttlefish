if [ "${BASH_SOURCE[0]}" -ef "$0" ]; then
	echo "source this script, do not execute it!"
	exit 1
fi

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

function cvd_allocate_instance_id {
	local -a ids=()
	for instance in $(docker ps -aq --filter="ancestor=cuttlefish"); do
		local id;
		id=$(docker inspect -f '{{- printf "%s" .Config.Labels.cf_instance}}' "${instance}")
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
	echo "   cvd_docker_create <options> [NAME] # by default names 'cuttlefish'"
	echo "     Options:"
	echo "       -s | --singleshot                : run the container, log in once, then delete it on logout"
	echo "                                        : otherwise, the container is created as a daemon"
	echo "       -x | --with_host_x               : run the container in singleshot and"
	echo "                                        : share X of the docker host"
	echo "       -A[/path] | --android[=/path]    : mount Android images from path (defaults to \$ANDROID_PRODUCT_OUT);"
	echo "                                        : requires -C to also be specified"
	echo "                                        : (Optional path argument must follow short-option name without intervening space;"
	echo "                                        :  it must follow long-option name followed by an '=' without intervening space)"
	echo "       -C[/path] | --cuttlefish[=/path] : mount Cuttlefish host image from path (defaults to \$ANDROID_HOST_OUT/cvd-host_package.tar.gz)"
	echo "                                        : (Optional path argument must follow short-option name without intervening space;"
	echo "                                        :  it must follow long-option name followed by an '=' without intervening space)"
	echo "       -m | --share_dir dir1:dir2       : mount a host directory dir1 at dir2 of docker container"
	echo "                                        : dir1 should be an absolute path or relative path to $PWD"
	echo "                                        : dir2 should be an absolute path or relative path to /home/vsoc-01/"
	echo "                                        : $HOME is not allowed as dir1"
	echo "                                        : /home/vsoc-01 is not allowed as dir2"
	echo "                                        : For multiple mounts, use multiple -m options per pair"
	echo "       -h | --help                      : print this help message"
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

    # s | --singleshot
    # x | --with_host_x
    # m | --share_dir dir1:dir2
    # A | --android[=/PATH]
    # C | --cuttlefish[=/PATH to host package file]
    # h | --help

  local params
  if params=$(getopt -o 'm:A::C::sxh' -l 'share_dir:,android::,cuttlefish::,singleshot,with_host_x,help' --name "$0" -- "$@"); then
	  eval set -- "${params}"
	  while true; do
		  case "$1" in
			  -m|--share_dir)
				  share_dir="true"
				  shared_dir_pairs+=("$2")
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
						  cuttlefish="${ANDROID_HOST_OUT}/cvd-host_package.tar.gz"
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

    local -a volumes=()
    if [[ -z "${container}" ]]; then
	    echo "Container ${name} does not exist.";

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
                if ! echo ${sub} | egrep ":" > /dev/null; then
                    echo "${sub} is ill-formated. should be host_dir:mnt_dir" 1>&2
                    echo "try $0 --help" 1>&2
                    exit 10
                fi
                local host_dir="$(echo $sub | cut -d ':' -f 1)"
                local guest_dir="$(echo $sub | cut -d ':' -f 2)"
                if ! is_absolute_path ${host_dir}; then
                    host_dir="${host_pwd}/${subdir}"
                fi
                if ! is_absolute_path ${guest_dir}; then
                    guest_dir="${guest_home}/${guest_dir}"
                fi
                host_dir=${realpath $host_dir} # resolve symbolic link only on the host side
                volumes+=("-v ${host_dir}:${guest_dir}")
            done
        fi

	    local -a as_host_x=()
	    if [[ "$with_host_x" == "true" ]]; then
		    as_host_x+=("-e DISPLAY=$DISPLAY")
		    as_host_x+=("-v /tmp/.X11-unix:/tmp/.X11-unix")
	    fi

        local cf_instance=$(cvd_allocate_instance_id)
        if [ "${cf_instance}" -gt 7 ]; then
                echo "Limit is maximum 8 Cuttlefish instances."
                return
        fi

        echo "Starting container ${name} (id ${cf_instance}) from image cuttlefish.";
	    docker run -d ${as_host_x[@]} \
		        --name "${name}" -h "${name}" \
                -l "cf_instance=${cf_instance}" \
                -e CUTTLEFISH_INSTANCE="${cf_instance}" \
                -p $((6443+cf_instance)):$((6443+cf_instance)) \
                -p $((8443+cf_instance)):$((8443+cf_instance)) \
                -p $((6250+cf_instance)):$((6250+cf_instance)) \
                -p $((15550+cf_instance*4)):$((15550+cf_instance*4))/tcp \
                -p $((15551+cf_instance*4)):$((15551+cf_instance*4))/tcp \
                -p $((15552+cf_instance*4)):$((15552+cf_instance*4))/tcp \
                -p $((15553+cf_instance*4)):$((15553+cf_instance*4))/tcp \
                -p $((15550+cf_instance*4)):$((15550+cf_instance*4))/udp \
                -p $((15551+cf_instance*4)):$((15551+cf_instance*4))/udp \
                -p $((15552+cf_instance*4)):$((15552+cf_instance*4))/udp \
                -p $((15553+cf_instance*4)):$((15553+cf_instance*4))/udp \
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

	local login_func
	local start_func
	local stop_func
	local gethome_func

	read -r -d '' login_func <<EOF
function $(__gen_login_func_name ${name}) {
  local _cmd="/bin/bash"
  if [[ -n "\$@" ]]; then
	_cmd="\$@"
  fi
  docker exec -it --user vsoc-01 "${name}" \${_cmd}
#  ssh \
#    -L8443:localhost:8443 \
#    -L6520:localhost:6520 \
#    -L6444:localhost:6444 \
#    -L15550:localhost:15550 -L15551:localhost:15551 \
#	\${_x} \
#    vsoc-01@$(cvd_get_ip ${name}) \
#	"\$@";
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

if [[ "$singleshot" == "false" ]]; then
    echo "To log into container ${name} without starting Android, call $(__gen_login_func_name ${name})"
    echo "To start Android in container ${name}, call $(__gen_start_func_name ${name})"
    echo "To stop Android in container ${name}, call $(__gen_stop_func_name ${name})"
    echo "To get the home directory of container ${name}, call $(__gen_gethome_func_name ${name})"
fi
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
done
