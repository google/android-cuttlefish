# Getting started with Cuttlefish host tool development

You can get started with these steps to build the host tools on Debian and
run a cuttlefish instance with them.

For installing the prebuilt packages instead, see the top-level
[README](../README.md).

## Install build dependencies

```sh
sudo apt install git libc6-dev libcap-dev libclang-dev libgbm-dev \
    libgcc-14-dev libopus-dev libwayland-dev libxml2 make \
    protobuf-compiler python3
```

<!-- Keep in sync with base/cvd/.bazelversion. -->
Bazel itself is managed by [bazelisk](https://github.com/bazelbuild/bazelisk);
`tools/buildutils/installbazel.sh` installs it. Alternatively, install
Bazel 8.5.1 from [GitHub](https://github.com/bazelbuild/bazel/releases) or
the [apt repository](https://bazel.build/install/ubuntu).

## Build `cvd`

```sh
cd base/cvd
bazel build //cuttlefish/package:cvd
```

The first build downloads and compiles all third-party dependencies and
takes a while. The resulting binary is `bazel-bin/cuttlefish/package/cvd`.

## Fetch an Android build

```sh
./bazel-bin/cuttlefish/package/cvd fetch \
    --default_build=aosp-android-latest-release/aosp_cf_x86_64_only_phone-userdebug \
    --target_directory=/tmp/cf
```

This downloads the device images and matching host tools from
[ci.android.com](https://ci.android.com/).

## Start the device

```sh
./bazel-bin/cuttlefish/package/cvd create \
    --host_path=/tmp/cf --product_path=/tmp/cf
```

`--host_path` and `--product_path` point at the fetched host tools and
device images respectively. Once booted, connect with `adb connect
localhost:6520` or open <https://localhost:8443> to interact with the
device over WebRTC.

## Next steps

* [Developing `cvd`](../base/cvd/README.md): running tests, generating
  `compile_commands.json`.
* [Host tools migration notes](HostToolsMigration.md).
* `tools/buildutils/build_packages.sh` builds the Debian packages, which
  are what we release; `base/debian/control` is the source of truth for
  their dependencies.
