# Create a GCE image for orchestrating Cuttlefish instances

## Setup

```
gcloud auth application-default login
```

## Step 1. Create image with wanted kernel version

Go to Step 2 if you have already an image with the wanted kernel.

`-linux-image-deb` is optional. Without it, the newest kernel available for the
Debian release (backports included) that the resolved NVIDIA driver builds
against is installed. By default, the NVIDIA driver tracks GCP's recommended
LTS branch (`gcp-lts`) using open kernel modules (`kernel-open`); override with
`-nvidia-branch` or `-nvidia-version`.

```
go run ./cmd/create_gce_fixed_kernel \
  -project <project> \
  -linux-image-deb linux-image-6.1.0-40-cloud-amd64 \
  -image-name <fixed_kernel_image_name>
```

## Step 2. Create base image

Go to Step 3 if you have already a base image.

Without explicit `-nvidia-*` flags, `create_gce_base_image` inherits the
validated NVIDIA driver stamped on the Step 1 image (or resolves the same
defaults on unlabeled images).

```
go run ./cmd/create_gce_base_image \
  -project <project> \
  -source-image-project <project> \
  -source-image <fixed_kernel_image_name> \
  -image-name <base_image_name>
```

## Step 3. Create image with cuttlefish debian packages installed.

```
go run ./cmd/gce_install_cuttlefish_packages \
  -project <project> \
  -source-image-project <project> \
  -source-image <base_image_name> \
  -image-name <output_image_name> \
  -deb <path/to/cuttlefish-base-deb> \
  -deb <path/to/cuttlefish-user-deb> \
  -deb <path/to/cuttlefish-orchestration-deb>
```

## Step 4. Validate output image

```
go run ./cmd/gce_validate_image \
  -project <project> \
  -image-project <project> \
  -image <output_image_name>
```
