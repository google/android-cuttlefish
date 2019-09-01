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

DEFAULTNET=$1
if [ "$DEFAULTNET" == "" ]; then
	warn1=0
	warn2=0
	attempts=0
	sleep_time=0.1
	warn_after=2.0
	max_attempts=`echo "(${warn_after}/${sleep_time})-1" | bc`
	while true; do
		DEFAULTNET=`ip link | grep "state UP" | sed 's/[0-9]*: \([^:]*\):.*/\1/'`
		if [[ "${DEFAULTNET}" == "" ]]; then
			if [[ $warn1 -eq 0 ]]; then
				echo "error: couldn't detect any connected default network"
				warn1=1
				warn2=0
			fi
			continue
		elif [ `echo "$DEFAULTNET" | wc -l` -eq 1 ]; then
			break
		elif [ `echo "$DEFAULTNET" | wc -l` -ne 1 ]; then
			if [[ $attempts -eq 0 ]]; then
				echo "Please disconnect the network cable from the Rock Pi"
			fi
			if [[ $warn2 -eq 0 ]] && [[ $attempts -ge $max_attempts ]]; then
				echo "error: detected multiple connected networks, not sure which to use as default:"
				for net in $DEFAULTNET; do echo "    $net"; done
				warn1=0
				warn2=1
			fi
			sleep $sleep_time
			attempts=$((attempts+1))
		fi
	done
	echo "Found default network at ${DEFAULTNET}"
	echo "Attach network cable from Rock Pi to PC's spare network port"
fi

# escalate to superuser
if [ "$UID" -ne 0 ]; then
	exec sudo bash "$0" "${CORPNET}"
fi

warn3=0
attempts=0
while true; do
	ROCKNET=`ip link | grep "state UP" | grep -v $DEFAULTNET | sed 's/[0-9]*: \([^:]*\):.*/\1/' | awk 'NF'`
	if [[ "${ROCKNET}" == "" ]]; then
		continue
	elif [ `echo "$ROCKNET" | wc -l` -eq 1 ]; then
		break
	elif [ `echo "$ROCKNET" | wc -l` -gt 1 ]; then
		if [[ $attempts -eq 0 ]]; then
			echo "Please keep the default network and rock pi network connected; disconnect the rest"
		fi
		if [[ $warn3 -eq 0 ]]; then
			echo "error: detected multiple additional networks, not sure which is the Rock Pi:"
			for net in $ROCKNET; do echo "    $net"; done
			warn3=1
		fi
		sleep $sleep_time
		attempts=$((attempts+1))
	fi
done
echo "Found Rock Pi network at ${ROCKNET}"
sudo ifconfig ${ROCKNET} down

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

echo "Restarting network interface..."
service network-manager restart
if [ $? != 0 ]; then
	echo "error: failed to restart network-manager"
	exit 1
fi
service networking restart
if [ $? != 0 ]; then
	echo "error: failed to restart networking"
	exit 1
fi

# Verify the Rock Pi was configured correctly
ip link show ${ROCKNET} >/dev/null
if [ $? != 0 ]; then
	echo "error: wasn't able to successfully configure connection to Rock Pi"
	exit 1
fi

# Check if dnsmasq is already installed
dpkg -l | grep " dnsmasq " >/dev/null
if [ $? != 0 ]; then
	echo "Installing dnsmasq..."
	apt-get install dnsmasq >/dev/null
fi

echo "Enabling dnsmasq daemon..."
cat /etc/default/dnsmasq | grep "ENABLED" >/dev/null
if [ $? == 0 ]; then
	sed -i 's/.*ENABLED.*/ENABLED=1/' /etc/default/dnsmasq
else
	sed -i 's/.*ENABLED.*/ENABLED=1/' /etc/default/dnsmasq
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

echo "Restarting dnsmasq service..."
service dnsmasq restart
if [ $? != 0 ]; then
	echo "error: failed to restart dnsmasq"
	exit 1
fi

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
