#
# included by setup.sh
# utility functions for port publishing for docker
#


# forward host tcp port to $2:$3/tcp
function socat_tcp() {
    nohup socat tcp4-listen:"$1",fork TCP:"$2":"$3" > /dev/null 2>&1 &
}

# forward host udp port to $2:$3/udp
function socat_udp() {
    nohup socat udp4-listen:"$1",fork UDP:"$2":"$3" > /dev/null 2>&1 &
}

# socat a range of ports unconditionally
#
# 1: container name
# 2: container ip
# 3: host base
# 4: guest base
# 5: number of consecutive ports to forward
# 6: "tcp" or "udp"
#
function socat_ranges() {
    local name=$1
    local ip=$2
    local host_base=$3
    local guest_base=$4
    local size=$5
    local mode=${6:-"tcp"} # tcp or udp
    size=$((size - 1))
    for i in $(seq 0 $size); do
        host_port=$((host_base + i))
        guest_port=$((guest_base + i))
        socat_${mode} $host_port $ip $guest_port > /dev/null 2>&1
    done
}

#
# main function that exposes traditional cuttlefish host ports
#
function port_expose() {
    local name=$1
    local guest_ip=$2
    local sz=$3 # maximum number of instances inside a container
    local host_offset=$4
    local guest_offset=$5
    local -a tcp_bases=( "6444" "8443" "6520" )

    for t in ${tcp_bases[@]}; do
        socat_ranges ${name} $guest_ip $((t + host_offset)) $((t + guest_offset)) $sz "tcp"
    done

    local modes=( "tcp" "udp" )
    for m in ${modes[@]}; do
        socat_ranges ${name} $guest_ip $((15550 + host_offset)) $((15550 + guest_offset)) 8 $m
    done
}

function port_close() {
    local ip_addr=$1
    local -a proc2kill=($(ps -t | egrep socat | egrep $1 | egrep -v grep | \
                              egrep -o "^[[:space:]]+[0-9]+[[:space:]]"))
    for pid in ${proc2kill[@]}; do
        kill -9 $pid
        wait $pid > /dev/null 2>&1
    done
}
