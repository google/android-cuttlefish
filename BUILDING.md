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
cd android-cuttlefish
./build.sh
```
# Starting and provisioning the container

## Creating and deleting the container

Set up your environment:

```bash
source setup.sh # once in each terminal window
```

Create a container:

```bash
cvd_docker_create <name> # <name defaults to cuttlefish>
```

Note that a few options are provided in creating the container:

```bash
cvd_docker_create -h
```

The options include but are not limited to:
1. pull the host packages and images from a host directory
2. mount the host directories to the docker container: log directory, .gitconfig, etc
3. run the docker container, log in once, then delete it on logout
   1. have the guest applications (e.g. Cuttlefish) use the host X server

```bash
cvd_docker_rm <name> # <name defaults to cuttlefish>
```

To list the Cuttlefish containers:

```bash
cvd_docker_list
```

## Installing the Cuttlefish and Android images on it

There are three ways to provision a container (that is, to install the Cuttlefish
and Android images on it.)

### Re-using a locally built Android

If cvd_docker_create detects an [Android build
environment](https://source.android.com/setup/build/building) that is set up to
build Cuttlefish, it will automatically set up the container to point to all the
binary artifacts.  Assuming you have downloaded, set up, and built a Cuttlefish
device and Android from the sources, and you call cvd_docker_create in the same
terminal, you can jump ahead to launching Cuttlefish by adding -A -C
option to cvd_docker_create

```
cvd_docker_create -A -C <other options> <name>
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
cvd_docker_create -C $HOME/android-src/out/host/linux-x86 -A
$HOME/android-src/out/target/product/vsoc_x86 <other options> <name>
```

Alternatively, you can manually set the two environment variables, and
give -A -C with default values:

```bash
export
ANDROID_PRODUCT_OUT=$HOME/android-src/out/target/product/vsoc_x86
export ANDROID_HOST_OUT=$HOME/android-src/out/host/linux-x86
cvd_docker_create -A -C $HOME/android-src <name>
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
# it was created with "cvd_docker_create cuttlefish"
ssh vsoc-01@${ip_cuttlefish} -- 'tar xzvf -' < cvd-host_package.tar.gz
scp *.img vsoc-01@${ip_cuttlefish}:~/
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
cvd_start_<name> options
```

For example, if you want to create acontainer named 'cf1' and launch Cuttlefish
with WebRTC support, 4GB of RAM and 4 CPUs:

```bash
source setup.sh
cvd_docker_create cf1
ssh vsoc-01@$ip_cf1 -- './download-aosp $(uname -m)'
cvd_start_cf1 --start_webrtc --cpus 4 --memory_mb 4096
```

As of now, cuttlefish is connecting the adb server inside the docker container by default. If it is intended to adb running outside, --norun_adb_connector should be given and connected manually:

```bash
cvd_start_cf --norun_adb_connector --start_webrtc --cpus 4 --memory_mb 4096
```

Following that, open a(nother) terminal, and type:

```bash
adb connect ip_cf1:6520
```

Once done, you stop cuttlefish as follows (in a new terminal)

```bash
source setup.sh
cvd_stop_cf1
```

Each container's IP address will be saved in variable $ip_<name>, where <name>
is the name of the container.  In the example above, the IP address would be in
variable $ip_cf1.

# Connecting to Cuttlefish

## ADB

To connect to Cuttlefish using ADB from outside the container:

```bash
adb connect localhost:6520
adb shell
```

Please note that --norun_adb_connector should have been given to the cvd_start_<name> command. 

Or from the inside (assuming you had set up, provisioned, and booted a container
called cf1):

```bash
ssh vsoc-01@$ip_cf1 -- ./bin/adb -e shell
```

## VNC

Please note that VNC is not enabled by default. --start_vnc_server --nostart_webrtc should be given
to cvd_start_<name>:

```bash
cvd_start_cf1 --nostart_webrtc --start_vnc_server --cpus 4 --memory_mb 4096
```

For now, cuttlefish VNC server is listening on 127.0.0.1:6444 or 6444+i port. That being said, VNC
server that runs in a container is currently not reached by a VNC client outside the container.

These are being fixed. In the meantime, there are two ways to workaround: SSH tunneling and redirecting
the VNC server to the host X server.

### SSH Tunneling

One way to avoid this is ssh tunneling:

```bash
source setup.sh
ssh -L 7444:$ip_cf1:6444 vsoc-01@$ip_cf1
```

Follwing that, open a new terminal. VNC server is effectively listening to localhost:7444. 

You can see the display using [VNC](https://android.googlesource.com/device/google/cuttlefish/#so-you-want-to-see-cuttlefish) as well. Follow the link to download the VNC viewer. Assuming you've saved it in your current working directory:

```bash
java -jar tightvnc-jviewer.jar -ScalingFactor=50 -Tunneling=no -host=localhost -port=7444
```

### Redirecting VNC Client Inside Container to Host X Server

Alternatively, you can run a VNC client inside the container, and redirect the X requests to the host X server.
As a result, the VNC client will act as if it was the host desktop X application. In other words, the VNC client
will paint its windows on the host desktop. For that, the container should have been created with -x option:

```bash
cvd_docker_create -x -A -C $HOME/android-src cf1
```

Once cuttlefish is launched, the following command from another terminal will launch a VNC client:

```bash
source setup.sh
cvd_login_cf1 vncviewer
```

The client will ask the VNC server address to connect, and this is the value to be used:
```bash
127.0.0.1:6444
```
## WebRTC

As an alternative to VNC, you can connect cuttlefish via webRTC. you can
connect (on the same machine as the docker container) by pointing your browser
at https://localhost:8443/

WebRTC allows you to control cuttlefish inside the docker container without ssh tunnel.

Using WebRTC requires installing `socat` binary to the host.
