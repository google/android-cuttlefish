# Building the Docker image

First, install docker and git, and modprobe vhost_vsock:

```bash
curl https://get.docker.com | sh
sudo apt install -y git
sudo modprobe vhost_vsock
```

Then the Docker image can be built as follows:

```bash
git clone https://github.com/google/android-cuttlefish.git
cd android-cuttlefish
./build.sh
```
# Starting and provisioning the container

Set up your environment:

```bash
source setup.sh # once in each terminal window
```

To create a container:

```bash
cvd_docker_create <name> # <name defaults to cuttlefish>
```

To stop and delete a container:

```bash
cvd_docker_rm <name> # <name defaults to cuttlefish>
```

To list the Cuttlefish containers:

```bash
cvd_docker_list
```

There are three ways to provision a container (that is, to install the Cuttlefish
and Android images on it.)

If cvd_docker_create detects an [Android build
environment](https://source.android.com/setup/build/building) that is set up to
build Cuttlefish, it will automatically set up the container to point to all the
binary artifacts.  Assuming you have downloaded, set up, and built a Cuttlefish
device and Android from the sources, and you call cvd_docker_create in the same
terminal, you can jump ahead to launching Cuttlefish.

If you do not have the Android build environment, alternatively, you
can build Android and Cuttlefish with another docker container. We are
offering a wrapper script for that. Say, the source is downloaded to
$HOME/android-src, we can run these to commands to build Android from
the source:
```bash
./build-android-with-docker.sh --help
./build-android-with-docker.sh --android-src-mntptr "$HOME/android-src:/home/vsoc-01/build"
```

That does build Android and Cuttlefish, and save them where you
downloaded the source. It does not, however, allow you to automatically jump ahead to launching
Cuttlefish. We are improving the process but for now, these commands
will prepare for running Cuttlefish with a container:
```bash
export ANDROID_BUILD_TOP=$HOME/android-src
export ANDROID_PRODUCT_OUT=$(dirname `find $ANDROID_BUILD_TOP/out |
egrep "*/boot\.img$"`)
export ANDROID_HOST_OUT=$(dirname `find $ANDROID_BUILD_TOP/out/host/ |
egrep "/bin/launch_cvd$")
```
After those commands, you can run the docker image and run the
container.

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
ssh vsoc-01@${ip_cuttlefish} -- './download-aosp $(uname -m)'
```

Note that if you want to run Cuttlefish built for x86, you will probably have to
invoke ./download-aosp with the argument 'x86', since your machine will most
likely be 'x86_64':

```bash
ssh vsoc-01@${ip_cuttlefish} -- './download-aosp x86'
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

Once done, you stop cuttlefish as follows (in a new terminal)

```bash
source setup.sh
cvd_stop_cf1
```

Each container's IP address will be saved in variable $ip_<name>, where <name>
is the name of the container.  In the example above, the IP address would be in
variable $ip_cf1.

# Connecting to Cuttlefish

To connect to Cuttlefish using ADB and VNC from outside the container:

```bash
adb connect localhost:6520
adb shell
```

Or from the inside (assuming you had set up, provisioned, and booted a container
called cf1):

```bash
ssh vsoc-01@$ip_cf1 -- ./bin/adb -e shell
```

You can see the display using [VNC](https://android.googlesource.com/device/google/cuttlefish/#so-you-want-to-see-cuttlefish) as well. Follow the link to download the VNC viewer. Assuming you've saved it in your current working directory:

```bash
java -jar tightvnc-jviewer.jar -ScalingFactor=50 -Tunneling=no -host=localhost -port=6444
```

Alternatively, if you launched Cuttlefish with the -start_webrtc option, you can
connect (on the same machine as the docker container) by pointing your browser
at https://localhost:8443/
