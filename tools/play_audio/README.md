# Play Audio

Audio receiver for a cuttlefish instance. A binary to play audio from a remote
Android virtual device.

## Install Dependencies

```
git clone https://github.com/google/opuscpp
sudo apt install libsdl2-2.0-0 libsdl2-dev libopus-dev libopus0 libgflags-dev libgoogle-glog-dev
```

## Build

```
make
```

## Run
Use ssh port forwarding to forward `7444` from a running instance. Then run the
`play_audio` binary.

```
./play_audio
```

If you are running multiple virtual devices on a host, you must pass the
`device_num` flag to specify the device corresponding to the `vsoc-##` username,
and you will have to add `1 - device_num` to the port that you forward (vsoc-02
= port 7445).
