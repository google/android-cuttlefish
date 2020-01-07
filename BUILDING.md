# Building

This code is intended for use with the cuttlefish Android device. A script
is provided in the AOSP tree:

https://android.googlesource.com/device/google/cuttlefish_common/+/master/tools/create_base_image.sh

The Docker image can be built as follows:

	git clone https://github.com/google/android-cuttlefish.git
	cd android-cuttlefish
	docker build -t cuttlefish https://github.com/google/android-cuttlefish.git

To create a container form this image:

	docker run -d --name cuttlefish -h cuttlefish --privileged -v /sys/fs/cgroup:/sys/fs/cgroup:ro cuttlefish

The container's IP address can be found as follows:

	CF=$(docker inspect --format='{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' cuttlefish)

You can then provision the container with a cuttlefish image as described, for example, using [these instructions on AOSP](https://android.googlesource.com/device/google/cuttlefish/), you can follow the steps to obtain a prebuilt image of Cuttlefish (start from step #2, since the container already took care of it). Once you've obtained the image at step #8, you can copy it to your container as follows:

```bash
ssh vsoc-01@$CF -- 'tar xzvf -' < cvd-host_package.tar.gz
scp *.img vsoc-01@$CF:~/
```

You can boot Cuttlefish from the container as follows:

	ssh vsoc-01@$CF \
		-L localhost:6520:127.0.0.1:6520 \
		-L localhost:6444:127.0.0.1:6444 \
		-- bin/launch_cvd -cpus 4 -memory_mb 4096

The above example launches Cuttlefish with 4 cores and 4GB of RAM, while also setting up an SSH tunnel that allows you to connect to Cuttlefish via ADB and VNC from outside the container:

```bash
adb connect localhost:6520
adb shell
```

Or from the inside:

	ssh vsoc-01@$CF -- ./bin/adb -e shell

You can see the display using [VNC](https://android.googlesource.com/device/google/cuttlefish/#so-you-want-to-see-cuttlefish) as well. Follow the link to download the VNC viewer. Assuming you've saved it in your current working directory:

	java -jar tightvnc-jviewer.jar -ScalingFactor=50 -Tunneling=no -host=localhost -port=6444
