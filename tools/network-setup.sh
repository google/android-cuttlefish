#!/bin/bash

# Copyright 2019 Google Inc. All rights reserved.

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#     http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

if [[ "$OSTYPE" != "linux-gnu" ]]; then
	echo "error: must be running linux"
	exit 1
fi

# escalate to superuser
if [ "$UID" -ne 0 ]; then
	exec sudo bash "$0"
fi

cleanup() {
	echo "Starting up network-manager..."
	service network-manager start
	if [ $? != 0 ]; then
		echo "error: failed to start network-manager"
		exit 1
	fi

	echo "Starting up networking..."
	service networking start
	if [ $? != 0 ]; then
		echo "error: failed to start networking"
		exit 1
	fi
	if [ ! -z "$1" ]; then
		exit $1
	fi
}

sleep_time=0.1
max_attempts=100
DEFAULTNET=$1
if [ "$DEFAULTNET" == "" ]; then
	warn_no_default_network=0
	warn_disconnect_rockpi=0
	attempts=0
	while true; do
		NETLIST=`ip link | grep "state UP" | sed 's/[0-9]*: \([^:]*\):.*/\1/'`
		if [[ "${NETLIST}" == "" ]]; then
			if [[ $warn_no_default_network -eq 0 ]]; then
				echo "error: couldn't detect any connected default network"
				warn_no_default_network=1
			fi
			continue
		elif [ `echo "${NETLIST}" | wc -l` -eq 1 ]; then
			DEFAULTNET=${NETLIST}
			break
		elif [ `echo "${NETLIST}" | wc -l` -ne 1 ]; then
			if [[ $warn_disconnect_rockpi -eq 0 ]]; then
				echo "Please disconnect the network cable from the Rock Pi"
				warn_disconnect_rockpi=1
			fi
			if [[ ${attempts} -gt ${max_attempts} ]]; then
				echo -e "\nerror: detected multiple connected networks, please tell me what to do:"
				count=1
				for net in ${NETLIST}; do
					echo "${count}) $net"
					let count+=1
				done
				read -p "Enter the number of your default network connection: " num_default
				count=1
				for net in ${NETLIST}; do
					if [ ${count} -eq ${num_default} ]; then
						echo "Setting default to: ${net}"
						DEFAULTNET=${net}
					fi
					let count+=1
				done
				warn_no_default_network=0
				break
			fi
			echo -ne "\r"
			printf "Manual configuration in %.1f seconds..." "$(( max_attempts-attempts ))e-1"
			sleep $sleep_time
		fi
		let attempts+=1
	done
fi
echo "Found default network at ${DEFAULTNET}"

if [ "${ROCKNET}" == "" ]; then
	echo "Please reconnect network cable from Rock Pi to PC's spare network port"
	attempts=0
	while true; do
		NETLIST=`ip link | grep "state UP" | grep -v $DEFAULTNET | sed 's/[0-9]*: \([^:]*\):.*/\1/' | awk 'NF'`
		networks=`echo "$NETLIST" | wc -l`
		if [[ "${NETLIST}" == "" ]]; then
			networks=0
		fi
		if [ $networks -eq 1 ]; then
			ROCKNET=${NETLIST}
			break
		elif [ $networks -gt 1 ]; then
			if [[ ${attempts} -gt ${max_attempts} ]]; then
				echo -e "\nerror: detected multiple connected networks, please tell me what to do:"
				count=1
				for net in ${NETLIST}; do
					echo "${count}) $net"
					let count+=1
				done
				read -p "Enter the number of your rock pi network connection: " num_rockpi
				count=1
				for net in ${NETLIST}; do
					if [ ${count} -eq ${num_rockpi} ]; then
						echo "Setting rock pi to: ${net}"
						ROCKNET=${net}
					fi
					let count+=1
				done
				break
			fi
			echo -ne "\r"
			printf "Manual configuration in %.1f seconds..." "$(( max_attempts-attempts ))e-1"
			let attempts+=1
		fi
		sleep $sleep_time
	done
fi
echo "Found Rock Pi network at ${ROCKNET}"
sudo ifconfig ${ROCKNET} down

echo "Downloading dnsmasq..."
apt-get install -d -y dnsmasq >/dev/null

echo "Shutting down network-manager to prevent interference..."
service network-manager stop
if [ $? != 0 ]; then
	echo "error: failed to stop network-manager"
	cleanup 1
fi

echo "Shutting down networking to prevent interference..."
service networking stop
if [ $? != 0 ]; then
	echo "error: failed to stop networking"
	cleanup 1
fi

echo "Installing dnsmasq..."
apt-get install dnsmasq >/dev/null

echo "Enabling dnsmasq daemon..."
cat /etc/default/dnsmasq | grep "ENABLED" >/dev/null
if [ $? == 0 ]; then
	sed -i 's/.*ENABLED.*/ENABLED=1/' /etc/default/dnsmasq
else
	echo "ENABLED=1" >> /etc/default/dnsmasq
fi

echo "Configuring dnsmasq for Rock Pi network..."
cat >/etc/dnsmasq.d/${ROCKNET}.conf << EOF
interface=${ROCKNET}
bind-interfaces
except-interface=lo
dhcp-authoritative
leasefile-ro
port=0
dhcp-range=192.168.0.100,192.168.0.199
EOF

echo "Configuring udev rules..."
cat >/etc/udev/rules.d/82-${ROCKNET}.rules <<EOF
ACTION=="add", SUBSYSTEM=="net", KERNEL=="${ROCKNET}", ENV{NM_UNMANAGED}="1"
EOF

echo "Configuring network interface..."
cat >/etc/network/interfaces.d/${ROCKNET}.conf <<EOF
auto ${ROCKNET}
iface ${ROCKNET} inet static
	address 192.168.0.1
	netmask 255.255.255.0
EOF

echo "Enabling IP forwarding..."
echo 1 >/proc/sys/net/ipv4/ip_forward

echo "Creating IP tables rules script..."
cat > /usr/local/sbin/iptables-rockpi.sh << EOF
#!/bin/bash
/sbin/iptables -A FORWARD -i ${ROCKNET} -o ${DEFAULTNET} -m state --state RELATED,ESTABLISHED -j ACCEPT
/sbin/iptables -A FORWARD -i ${ROCKNET} -o ${DEFAULTNET} -j ACCEPT
/sbin/iptables -t nat -A POSTROUTING -o ${DEFAULTNET} -j MASQUERADE
EOF
sudo chown root:root /usr/local/sbin/iptables-rockpi.sh
sudo chmod 750 /usr/local/sbin/iptables-rockpi.sh

echo "Creating IP tables rules service..."
cat > /etc/systemd/system/iptables-rockpi.service << EOF
[Unit]
Description=iptables rockpi service
After=network.target

[Service]
Type=oneshot
ExecStart=/usr/local/sbin/iptables-rockpi.sh
RemainAfterExit=true
StandardOutput=journal

[Install]
WantedBy=multi-user.target
EOF

echo "Reloading systemd manager configuration..."
sudo systemctl daemon-reload

echo "Start IP tables rules service..."
sudo systemctl enable iptables-rockpi
sudo systemctl start iptables-rockpi

cleanup

echo "Restarting dnsmasq service..."
service dnsmasq restart
if [ $? != 0 ]; then
	echo "error: failed to restart dnsmasq"
	exit 1
fi

# Verify the Rock Pi was configured correctly
ip link show ${ROCKNET} >/dev/null
if [ $? != 0 ]; then
	echo "error: wasn't able to successfully configure connection to Rock Pi"
	exit 1
fi

echo "Searching for Rock Pi's IP address..."
while true; do
	rockip=`cat /proc/net/arp | grep ${ROCKNET} | grep -v 00:00:00:00:00:00 | cut -d" " -f1`
	if [[ ${#rockip} -ge 7 ]] && [[ ${#rockip} -le 15 ]]; then
		break
	fi
	sleep 0.1
done

echo "Writing Rock Pi configuration to ~/.ssh/config..."
USER_HOME=$(getent passwd $SUDO_USER | cut -d: -f6)
grep -w "Host rock01" $USER_HOME/.ssh/config > /dev/null 2>&1
if [ $? != 0 ]; then
	cat >>$USER_HOME/.ssh/config << EOF
Host rock01
    HostName ${rockip}
    User vsoc-01
    IdentityFile ~/.ssh/rock01_key
    LocalForward 6520 127.0.0.1:6520
    LocalForward 6444 127.0.0.1:6444
EOF
else
	sed -i '/Host rock01/{n;s/.*/    HostName '${rockip}'/}' $USER_HOME/.ssh/config
fi
grep -w "Host rockpi01" $USER_HOME/.ssh/config > /dev/null 2>&1
if [ $? != 0 ]; then
	cat >>$USER_HOME/.ssh/config << EOF
Host rockpi01
    HostName ${rockip}
    User vsoc-01
    IdentityFile ~/.ssh/rock01_key
EOF
else
	sed -i '/Host rockpi01/{n;s/.*/    HostName '${rockip}'/}' $USER_HOME/.ssh/config
fi

sudo chown $SUDO_USER:`id -ng $SUDO_USER` $USER_HOME/.ssh/config
sudo chmod 600 $USER_HOME/.ssh/config

echo "Creating ssh key..."
sudo -u $SUDO_USER echo "n" | sudo -u $SUDO_USER ssh-keygen -q -t rsa -b 4096 -f $USER_HOME/.ssh/rock01_key -N '' >/dev/null 2>&1
tmpfile=`mktemp`
echo "echo cuttlefish" > "$tmpfile"
chmod a+x "$tmpfile"
chown $SUDO_USER "$tmpfile"
sudo SSH_ASKPASS="${tmpfile}" DISPLAY=:0 su $SUDO_USER -c "setsid -w ssh-copy-id -i ${USER_HOME}/.ssh/rock01_key -o StrictHostKeyChecking=no vsoc-01@${rockip} >/dev/null 2>&1"
if [ $? != 0 ]; then
	sed -i "/${rockip}/d" ${USER_HOME}/.ssh/known_hosts
	sudo SSH_ASKPASS="${tmpfile}" DISPLAY=:0 su $SUDO_USER -c "setsid -w ssh-copy-id -i ${USER_HOME}/.ssh/rock01_key -o StrictHostKeyChecking=no vsoc-01@${rockip} >/dev/null 2>&1"
	if [ $? != 0 ]; then
		echo "error: wasn't able to connect to Rock Pi over ssh"
		exit 1
	fi
fi

echo "Successfully configured!"
echo "  Host: 192.168.0.1"
echo "RockPi: ${rockip}"
echo "SSH Alias: rock01 (auto port-forwarding)"
echo "SSH Alias: rockpi01 (no port-forwarding)"
