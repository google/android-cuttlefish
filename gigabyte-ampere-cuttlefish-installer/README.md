# Gigabyte Ampere Cuttlefish Installer

## Download image

With executing following command, you can download image from Artifact
Registry.

```
ARM_OS_IMAGE_VERSION=unstable # Modify the version if it's needed.
wget -O preseed-mini.iso.xz "https://artifactregistry.googleapis.com/download/v1/projects/android-cuttlefish-artifacts/locations/us/repositories/gigabyte-ampere-server-installer/files/debian-installer:${ARM_OS_IMAGE_VERSION}:preseed-mini.iso.xz:download?alt=media"
```

With executing following command, you can see which versions are available to
download from Artifact Registry.

```
gcloud auth login
gcloud artifacts versions list \
    --project=android-cuttlefish-artifacts \
    --location=us \
    --repository=gigabyte-ampere-server-installer \
    --package=debian-installer
```
