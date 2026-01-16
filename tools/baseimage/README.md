# Create a GCE image for orchestrating Cuttlefish instances

## Setup

```
gcloud auth application-default login
```

## Step 1. Create image with wanted kernel version

Go to Step 2 if you have already an image with the wanted kernel.

```
go run ./cmd/create_gce_fixed_kernel \
  -project <project> \
  -linux-image-deb linux-image-6.1.0-40-cloud-amd64 \
  -image-name <fixed_kernel_image_name>
```

## Step 2. Create base image

Go to Step 3 if you have already a base image.

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
