# Host side VNC server

## Build instructions

```shell
bazel build //host/vnc_server/:vnc_server
```

### Requirements

* libjpeg-turbo8-dev

## Run Instructions

The vnc server receives frame updates from the hwcomposer through the shared
memory, for which it needs to connect to the ivserver. To send input to the
instance it temporarily connects to monkey, which is set up to run on the
instance as a service and listens on port 6445, so you need to forward that port
to a local socket.

* Make sure to wait for monkey to start (about 16s), it's one of the last
services to start since it's started by the zygote:

```shell
adb shell ps -e | grep monkey
```

* Forward the port to a local socket:

```shell
adb forward localfilesystem:/tmp/android-cuttlefish-1-input tcp:6445
```

* Run the server:

```shell
bazel-bin/host/vnc_server/vnc_server --port=6444 --input_socket=/tmp/android-cuttlefish-1-input
```

The VNC server will then be listening for connections on port 6444 on the host.
