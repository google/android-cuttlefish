# Create a GCE image for orchestrating Cuttlefish instances

## Setup

```
gcloud auth application-default login
```

## Step 1. Create image with wanted kernel version

Go to Step 2 if you have already an image with the wanted kernel.

```
go run ./cmd/create_gce_x86_64_fixed_kernel \
  -project <project> \
  -linux-image-deb linux-image-6.1.0-40-cloud-amd64
```

## Step 2. Create base image

Go to Step 3 if you have already a base image.

```
go run ./cmd/create_gce_x86_64_image \
  -project <project> \
  -source-image-project <project> \
  -source-image <image-from-step-1>
```

## Step 3. Create image with cuttlefish debian packages installed.

```
go run ./cmd/gce_install_cuttlefish_packages \
  -project <project> \
  -source-image-project <project> \
  -source-image <image-from-step-2> \
  -deb <path/to/cuttlefish-base-deb> \
  -deb <path/to/cuttlefish-user-deb> \
  -deb <path/to/cuttlefish-orchestration-deb>
```
