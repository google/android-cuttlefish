# So you want to try cuttlefish?

1. Go to http://ci.android.com/
2. Enter a branch name. Start with `aosp-master` if you don't know what you're
   looking for
3. Navigate to `aosp_cf_x86_phone` and click on `userdebug` for the latest build
4. Click on `Artifacts`
5. Scroll down to the OTA images. These packages look like
   `aosp_cf_x86_phone-img-xxxxxx.zip` -- it will always have `img` in the name.
   Download this file
6. Scroll down to `cvd-host_package.tar.gz`. You should always download a host
   package from the same build as your images.
7. On your local system, combine the packages:

   `$ mkdir cf`
   `$ cd cf`
   `$ tar xvf /path/to/cvd-host_package.tar.gz`
   `$ unzip /path/to/aosp_cf_x86_phone-img-xxxxxx.zip`

8. Launch cuttlefish with:

   `$ HOME=$PWD ./bin/launch_cvd`

9. Stop cuttlefish with:

   `$ HOME=$PWD ./bin/stop_cvd`

# So you want to debug cuttlefish?

You can use `adb` to debug it, just like a physical device:

   `$ ./bin/adb -e shell`

# So you want to see cuttlefish?

You can use the [TightVNC JViewer](https://www.tightvnc.com/download.php). Once
you have downloaded the *TightVNC Java Viewer JAR in a ZIP archive*, run it with

   `$ java -jar tightvnc-jviewer.jar -ScalingFactor=50 -Tunneling=no -host=localhost -port=6444`

Click "Connect" and you should see a lock screen!
