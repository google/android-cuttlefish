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

*** promo
   ARM specific steps:
   - When running on an ARM machine, the most direct way is to check
   for the existence of `/dev/kvm`. Note that this method can also be used to
   confirm support of KVM on any environment.
***

2. Download, build, and install the host debian packages:

   ```bash
   sudo apt install -y git devscripts config-package-dev debhelper-compat golang curl
   git clone https://github.com/google/android-cuttlefish
   cd android-cuttlefish
   for dir in base frontend; do
     cd $dir
     debuild -i -us -uc -b -d
     cd ..
   done
   sudo dpkg -i ./cuttlefish-base_*_*64.deb || sudo apt-get install -f
   sudo dpkg -i ./cuttlefish-user_*_*64.deb || sudo apt-get install -f
   sudo usermod -aG kvm,cvdnetwork,render $USER
   sudo reboot
   ```

   The reboot will trigger installing additional kernel modules and applying
   udev rules.

3. Go to http://ci.android.com/
4. Enter a branch name. Start with `aosp-master` if you don't know what you're
   looking for
5. Navigate to `aosp_cf_x86_64_phone` and click on `userdebug` for the latest build

*** promo
   For ARM, use branch `aosp-master-throttled-copped` and device target `aosp_cf_arm64_only_phone-userdebug`
***

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

## Debug Cuttlefish

You can use `adb` to debug it, just like a physical device:

   `$ ./bin/adb -e shell`

## Launch Viewer (WebRTC)

When launching with `---start_webrtc` (the default), you can see a list of all
available devices at `https://localhost:8443` . For more information, see the
WebRTC on Cuttlefish
[documentation](https://source.android.com/setup/create/cuttlefish-ref-webrtc).

## Stop Cuttlefish

You will need to stop the virtual device within the same directory as you used
to launch the device.

   `$ HOME=$PWD ./bin/stop_cvd`

