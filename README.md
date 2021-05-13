# Cuttlefish Getting Started

## Try Cuttlefish

1. Make sure virtualization with KVM is available.

   ```bash
    grep -c -w "vmx\|svm" /proc/cpuinfo
    ```

   This should return a non-zero value. If running on a cloud machine, this may
   take cloud-vendor-specific steps to enable. For Google Compute Engine
   specifically, see the [GCE guide].

   [GCE guide]: https://cloud.google.com/compute/docs/instances/enable-nested-virtualization-vm-instances

2. Download, build, and install the host debian package:

   ```bash
   git clone https://github.com/google/android-cuttlefish
   cd android-cuttlefish
   debuild -i -us -uc -b
   sudo dpkg -i ../cuttlefish-common_*_amd64.deb || sudo apt-get install -f
   sudo reboot
   ```

   The reboot will trigger installing additional kernel modules and applying
   udev rules.

3. Go to http://ci.android.com/
4. Enter a branch name. Start with `aosp-master` if you don't know what you're
   looking for
5. Navigate to `aosp_cf_x86_64_phone` and click on `userdebug` for the latest build
6. Click on `Artifacts`
7. Scroll down to the OTA images. These packages look like
   `aosp_cf_x86_64_phone-img-xxxxxx.zip` -- it will always have `img` in the name.
   Download this file
8. Scroll down to `cvd-host_package.tar.gz`. You should always download a host
   package from the same build as your images.
9. On your local system, combine the packages:

   ```bash
   mkdir cf
   cd cf
   tar xvf /path/to/cvd-host_package.tar.gz
   unzip /path/to/aosp_cf_x86_64_phone-img-xxxxxx.zip
   ```

10. Launch cuttlefish with:

   `$ HOME=$PWD ./bin/launch_cvd`

11. Stop cuttlefish with:

   `$ HOME=$PWD ./bin/stop_cvd`

## Debug Cuttlefish

You can use `adb` to debug it, just like a physical device:

   `$ ./bin/adb -e shell`

## Launch Viewer (WebRTC)

When launching with `---start_webrtc` (the default), you can see a list of all
available devices at `https://localhost:8443` . For more information, see the
WebRTC on Cuttlefish
[documentation](https://source.android.com/setup/create/cuttlefish-ref-webrtc).

## Launch Viewer (VNC)

When launching with `--start_vnc_server=true` , You can use the
[TightVNC JViewer](https://www.tightvnc.com/download.php). Once you have
downloaded the *TightVNC Java Viewer JAR in a ZIP archive*, run it with

   `$ java -jar tightvnc-jviewer.jar -ScalingFactor=50 -Tunneling=no -host=localhost -port=6444`

Click "Connect" and you should see a lock screen!
