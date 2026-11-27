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

Using a directory containing the Cuttlefish `.deb` packages (auto-discovers the 6 packages, configures Host Orchestrator to use GCE metadata credentials, attaches the `base_image` label, and auto-generates `cuttlefish-image-<timestamp>--cvd<major>-<minor>` if `-image-name` is omitted):

```
go run ./cmd/gce_install_cuttlefish_packages \
  -project <project> \
  -arch x86_64 \
  -source-image-project <source_image_project> \
  -source-image <base_image_name> \
  -dest-image-project <dest_image_project> \
  -debs-dir <path/to/debs_dir>
```

Or specifying individual `.deb` files and an explicit output image name:

```
go run ./cmd/gce_install_cuttlefish_packages \
  -project <project> \
  -arch x86_64 \
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

## Step 5. [Optional] Create a resized copy of an image

```
go run ./cmd/gce_resize_image \
  -project <project> \
  -source-image-project <source_image_project> \
  -source-image <output_image_name> \
  -dest-image-project <dest_image_project> \
  -size-gb 160
```
