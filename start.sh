if [ "${BASH_SOURCE[0]}" -ef "$0" ]; then
	echo "source this script, do not execute it!"
	exit 1
fi

function get_id() {
	local _name;
	if [ -z "$1" ]; then
		_name='cuttlefish';
	else
		_name=$1;
	fi
	echo $_name;
}

function container_exists() {
	[[ $(docker ps --filter "name=^/$1$" --format '{{.Names}}') == $1 ]] && echo $1;
}

function start_container_if_necessary() {
	local _name;
	_name=$(container_exists $1)
	if [[ "${_name}" != "$1" ]]; then
		echo "Container $1 does not exist.";
		echo "Starting container $1 from image cuttlefish.";
		docker run -d \
			--name $1 -h $1 \
			--privileged \
			-v /sys/fs/cgroup:/sys/fs/cgroup:ro cuttlefish;
	else
		echo "Container ${_name} exists";
		if [ $(docker inspect -f "{{.State.Running}}" ${_name}) != 'true' ]; then
			echo "Container ${_name} is not running.";
		else
			echo "Container ${_name} is already running.";
		fi
	fi
}

function get_cuttlefish_ip() {
	local _name;
	_name=$(container_exists $1)
	[[ "${_name}" == "$1" ]] && \
		echo $(docker inspect --format='{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' $1)
}

start_container_if_necessary $(get_id $1)
export CF=$(get_cuttlefish_ip $(get_id $1))
echo 'You can now log in with ssh: ssh vsoc-01@$CF'
echo "Or with docker: docker exec -it --user vsoc-01 $(get_id $1) /bin/bash"
