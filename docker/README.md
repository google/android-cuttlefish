# Building the Docker image

First, install docker and git, and modprobe vhost_vsock and vhost_net:

```bash
curl https://get.docker.com | sh
sudo apt install -y git
sudo modprobe vhost_vsock vhost_net
```

Then the Docker image can be built as follows:

```bash
git clone https://github.com/google/android-cuttlefish.git
cd android-cuttlefish/docker
./build.sh
```
# Starting and provisioning the container

## Creating and deleting the container

Set up your environment:

*Option 1*: Use `setup.sh`:
```bash
source setup.sh # once in each terminal window
```

*Option 2*: Add the `cf` script in this directory to your `$PATH`.
Then any command beginning `cf_foo` listed below may be invoked as
`cf foo`. Any references to `$ip_foo` must be replaced with `cf
get_ip foo`. This option is ideal if you don't want to source the script
in every terminal window, or if you use a shell other than bash.

Create a container:

```bash
cf_docker_create <name> # <name defaults to cuttlefish>
```

Note that a few options are provided in creating the container:

```bash
cf_docker_create -h
```

The options include but are not limited to:
1. pull the host packages and images from a host directory
2. mount the host directories to the docker container: log directory, .gitconfig, etc
3. run the docker container, log in once, then delete it on logout
   1. have the guest applications (e.g. Cuttlefish) use the host X server

```bash
cf_docker_rm <name> # <name defaults to cuttlefish>
```

To list the Cuttlefish containers:

```bash
cf_docker_list
```

## Installing the Cuttlefish and Android images on it

There are three ways to provision a container (that is, to install the Cuttlefish
and Android images on it.)

### Re-using a locally built Android

If cf_docker_create detects an [Android build
environment](https://source.android.com/setup/build/building) that is set up to
build Cuttlefish, it will automatically set up the container to point to all the
binary artifacts.  Assuming you have downloaded, set up, and built a Cuttlefish
device and Android from the sources, and you call cf_docker_create in the same
terminal, you can jump ahead to launching Cuttlefish by adding -A -C
option to cf_docker_create

```
cf_docker_create -A -C <other options> <name>
```

### Building Android from the source with build-android-with-docker.sh

If you do not have the Android build environment, alternatively, you
can build Android and Cuttlefish with
build-android-with-docker.sh. This is a wrapper for another docker
container dedicated to build Cuttlefish and Android from the source.

The source could be downloaded by following the instructions
[here](https://source.android.com/setup/build/downloading). Once repo
sync is done, the source tree is ready for build.

Say, the source is downloaded to $HOME/android-src. The following
command will build the Android and Cuttlefish from the source.

```bash
./build-android-with-docker.sh --android_src_mnt "$HOME/android-src:/home/vsoc-01/build"
```

For more detail:
```bash
./build-android-with-docker.sh --help
```

The build-android-with-docker.sh script also allows:
1. running a docker guest command with arguments
2. mounting a host script at docker container to run it with arguments
3. passing another set of arguments to docker run at the same time
4. as a showcase, just init, sync, and build with default
   configurations

Note that if arguments should be passed to the guest command or the
host script, it should be done like this:
```bash
./build-android-with-docker.sh --android_src_mnt "$HOME/android-src:/home/vsoc-01/build" \
    --op_mode=guest guest_program -- -a -b -c and more arguments

```
-- makes the following arguments like -a, -b, -c, arguments, rather
than options to the ./build-android-with-docker script. Without --,
getopt that internally implements cmdline parsing will be confused.

Also, for --docker_run_opts, spaces are not allowed. Thus, for a short
docker option with optional parameter, you should do as follows:
```
-vVALUE1:VALUE2
```

For a long docker run option with optional parameter, you should do
like this:
```
--name=MYNAME
```

In summary, a docker_run_opts looks like:
```bash
--docker_run_opts="-vV1:V2,--name=MYNAME,--privileged,-a,-b"

```

Although the command will build Android and Cuttlefish, the docker
contaienr does not automatically detect the directory. To provision
the docker container with the freshly built Android and Cuttlefish, we
must give -C and -A options to specify the location of
cvd-host_package.tar.gz and the ANDROID_PRODUCT_OUT directory,
respectively.

Assuming that cvd-host_package.tar.gz file is in
$HOME/android-src/out/host/linux-x86 and the ANDROID_PRODUCT_OUT is
$HOME/android-src/out/target/product/vsoc_x86, the command would look like this:


```bash
cf_docker_create -C $HOME/android-src/out/host/linux-x86 -A
$HOME/android-src/out/target/product/vsoc_x86 <other options> <name>
```

Alternatively, you can manually set the two environment variables, and
give -A -C with default values:

```bash
export
ANDROID_PRODUCT_OUT=$HOME/android-src/out/target/product/vsoc_x86
export ANDROID_HOST_OUT=$HOME/android-src/out/host/linux-x86
cf_docker_create -A -C $HOME/android-src <name>
```

### Using pre-built cuttlefish image
If you you want to provision
the container with a prebuilt cuttlefish image for example, from [AOSP
CI](htts://ci.android.com), you can follow the steps to [obtain a prebuilt image
of Cuttlefish](https://android.googlesource.com/device/google/cuttlefish/)
(start from step #2, since the container already took care of it). Once you've
obtained the image at step #8, you can copy it to your container as follows:

```bash
# We assume the container is called 'cuttlefish' and
# it was created with "cf_docker_create cuttlefish"
# Both archives have been extracted to cf/ directory.
scp -r cf/* vsoc-01@${ip_cuttlefish}:~/
```

As an aid, if you would like to fetch the latest Cuttlefish build from AOSP:

```bash
ssh vsoc-01@${ip_cuttlefish} -- './download-aosp -A -C -a $(uname -m)'
```

-A and -C respectively enable the download of Android images and the
host package. -a indicates the architecture.

Note that if you want to run Cuttlefish built for x86, you will probably have to
invoke ./download-aosp with the arguments -a 'x86', since your machine will most
likely be 'x86_64':

```bash
ssh vsoc-01@${ip_cuttlefish} -- './download-aosp  -A -C -a x86'
```

# Launching Cuttlefish inside a container

To launch Cuttlefish within the container you've created:

```bash
cf_start_<name> options
```

For example, if you want to create acontainer named 'cf1' and launch Cuttlefish
with WebRTC support, 4GB of RAM and 4 CPUs:

```bash
source setup.sh
cf_docker_create cf1
ssh vsoc-01@$ip_cf1 -- './download-aosp $(uname -m)'
cf_start_cf1 --start_webrtc --cpus 4 --memory_mb 4096
```

As of now, cuttlefish is connecting the adb server inside the docker container by default. If it is intended to adb running outside, --norun_adb_connector should be given and connected manually:

```bash
cf_start_cf --norun_adb_connector --start_webrtc --cpus 4 --memory_mb 4096
```

Following that, open a(nother) terminal, and type:

```bash
adb connect ip_cf1:6520
```

Once done, you stop cuttlefish as follows (in a new terminal)

```bash
source setup.sh
cf_stop_cf1
```

Each container's IP address will be saved in variable $ip_<name>, where <name>
is the name of the container.  In the example above, the IP address would be in
variable $ip_cf1.

# Connecting to Cuttlefish

We recommend that each docker container should be assigned an ip address and that the users should access the contaienr via the ip. 

If assigning a public IP is not feasible, we offer a environment variable, ```ip_${name}``` and a bash function, ```cf_get_ip```.
Those returns the internal IP valid on the docker host. 

As the internal ip returned by ```cf_get_ip``` and also stored in
```ip_${name}``` is not recognized outside the docker host, if remote
access to the container is desired, extra steps are required. It is
described in the [IPvlan](#ipvlan) and [Port Forwarding](#port-forwarding)
sections.

## ADB

When the adb client is running outside the container either remotely or locally (on the docker host), the cuttlefish inside the container
could be accessed as follows:

```bash
adb connect $ip_cf1:6520
adb shell
```

Please note that --norun_adb_connector should have been given to the cf_start_<name> command. Also, ```$ip_cf1``` is the public or internal
ip address of the container. 

Alternatively, you could run adb inside the container, and use the adb client over ssh:

```bash
ssh vsoc-01@$ip_cf1 -- ./bin/adb -e shell
```

## WebRTC

You can connect to cuttlefish via webRTC. you can
connect (on the same machine as the docker container) by pointing your browser
at ```https://$container_ip:8443/```

WebRTC allows you to control cuttlefish inside the docker container without ssh tunnel.

Please note that now WebRTC is the default. Also, please note that we have tested WebRTC with Google
Chrome Browsers, not with Internet Explorers, Safari, Firefox, Opera, etc. 

### Using the container's IP address

Simply, from a web browser:

```
http://192.168.9.2:8443
```

Please note that you should replace ```192.168.9.2``` with the actual IP address of your container.

### Redirecting Google Chrome Inside Container to Host X Server

Alternatively, you can use the Google Chrome inside the container. Chrome will behave like an X application
running directly on the docker host. The idea is that we forward all X requests from the Chrome to the host
X server as opposed to the X server insied the container. Please note that you may have to close the Chrome 
already running on the host.

For that, the first step is to create the container with -x option:

```bash
cf_docker_create -x -A -C $HOME/android-src cf1
```


Once cuttlefish is launched, the following command from another terminal will launch Google Chrome:

```bash
source setup.sh
cf_login_cf1 google-chrome-stable
```

This Google Chrome renders on the host but runs inside the container. Thus, use the following IP address and
port to access to cuttlefish:

```
https://127.0.0.1:8443
```

Note that 127.0.0.1 is not meant to be the docker host.

### SSH Tunneling

We do not believe ssh tunneling works smoothly for WebRTC. WebRTC uses tcp and udp ports. Not sure if UDP ports are for 
ssh tunneling. 

## IPvlan

You can expose the container directly on a LAN by creating an IPvlan,
which is the recommended approach to avoid WebRTC issues associated
with port forwarding. This effectively creates an additional IP address
for your host that gets routed to the container. For example, to create
an IPvlan that assigns the IP address 192.168.1.123 to your container,
use something like the following command:

```bash
docker network create -d ipvlan --subnet=192.168.1.0/24 --gateway=192.168.1.1 --ip-range=192.168.1.123/32 -o parent=eth0 myvlan
```

See the [Docker documentation](https://docs.docker.com/network/ipvlan/)
for details. Once you've done this, you can specify the name of the
IPvlan when creating the container:

```bash
cf_docker_create --network myvlan
```

Make sure not to expose this IP address to the Internet, as Cuttlefish
will allow passwordless authentication as the user `vsoc-01` on the
SSH port.

## Port Forwarding

The assumption here is an IP address cannot be assigned to the docker container. Likely, the container should be accessed
remotely: from a client outside the docker host. Please let us remind the readers that it's not what we recommend.

One way to do that is to use a bunch of -p options to expose the port. In ```setup.sh```, there is a single ```docker run```
command. Feel free to add a bunch of -p options to exports host ports to the docker container. 

Alternatively, we offer an optional, convenience function: ```cf_publish_cf1``` where ```cf1``` is the name of the container.
The function exposes a set of host ports to the docker container. Internally, it uses ```socat```. As an example, by default, 
the adb port is 6520. Each container is automatically assigned a unique ```id``` when ```cf_docker_create``` creates it.
```cf_publish_cf1``` maps the host port, ```6520 + id```, to the docker container's adb port.

The ```cf_get_instance_id``` bash function returns the ```id```:

```bash
$ cf_get_instance_id "cf1"
0
```

For WebRTC, this may not work well. WebRTC wants to make sure that the server IP address that the client sends the request to should
match the IP the server is actually running. With this socat-based forwarding scheme, the client will sends the http(s) requests to the
docker host IP. However, the WebRTC server is actually running inside the container. 




