# Create a GCE image for orchestrating Cuttlefish instances

Run following commands at the root of the `android-cuttlefish` repo directory.

## Build tool

```
pushd tools/baseimage
go build ./cmd/create_gce_x86_64_image
popd
```

## Run tool

```
gcloud auth application-default login
tools/baseimage/create_gce_x86_64_image --project <project> [--zone <zone>]
```
