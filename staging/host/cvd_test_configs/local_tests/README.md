#How to run these tests

This folder contains .json configs that serve as examples for
launches of multi-device configurations, where one or more of
those devices are a local instance.  Some tests mix these local
instances with remotely fetched ones as well, but in all cases
at least one local image is needed. Hence some steps are needed
to create these artifacts locally.

In particular - there needs to be the following artifacts local:

1. A folder containing Cuttlefish 'host tools' -
which contains the `cvd` binary (among others) that are
used to launch the instances.

2. A device image folder stored locally, which will be run within
the VMs created, effectively demonstrating that the local instance
was successfully created.

There are a few recommended ways to set up these folders, feel
free to use whichever method works best for your use case.

## Option 1: Modify the configs manually after local AOSP build

1. Check out a copy of the AOSP source tree from the repository and
branch desired
2. Follow standard process to build AOSP.  When complete the AOSP build
process will put binaries into the `[AOSP checkout folder]`/out
3. Modify the .json files to point to correct folders. In particular there
are two paths that need to be replaced.  The first is `/tmp/vsoc_x86_64`, which
will be the folder containing the disk images for the local instance to be launched.
This will be located under the AOSP `out` folder at a path similair to:
`/out/target/product/vsoc_x86_64`.  One way to get this folder is via:
`env | grep ANDROID_PRODUCT_OUT=`.
The 2nd path to update is listed in the .json files as `/tmp/linux-x86`, and
this is a path containing the Cuttlefish host tools binaries.  This can
also be obtained after an AOSP build, while in the same shell, via:
`env | grep ANDROID_HOST_OUT=`.
4. Tests can now be executed via `cvd load host/cvd_test_configs/local_mixed_1.json`
(ensuring you give the correct path and changing the filename
to whichever test you'd like to execute).

## Option 2: Use included `gen_local_configs.sh` script

1. Check out AOSP tree and run a build.  While still in the same shell that executed
the build, move to step 2. (Or use `lunch` to init the environment in a shell.)
2. Included in the cuttlefish tree is a script, which when executed will
copy all included .json files to /tmp, and update the configs with paths matching your
current AOSP local environment. Run this script while in the folder containing this README,
via `../../../tools/testing/gen_local_configs.sh`.
3. Tests can now be executed via `cvd load /tmp/local_mixed_1.json` (changing the filename
to whichever test you'd like to execute).  The Android image used within the tests (as the
local VM) is the same one which you have just compiled.
