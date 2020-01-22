#!/bin/bash

# Common code to build a host image on GCE

# INTERNAL_extra_source may be set to a directory containing the source for
# extra package to build.

# INTERNAL_IP can be set to --internal-ip run on a GCE instance
# The instance will need --scope compute-rw

source "${ANDROID_BUILD_TOP}/external/shflags/src/shflags"

DEFINE_string build_instance \
  "${USER}-build" "Instance name to create for the build" "i"
DEFINE_string build_project "$(gcloud config get-value project)" \
  "Project to use for scratch"
DEFINE_string build_zone "$(gcloud config get-value compute/zone)" \
  "Zone to use for scratch resources"
DEFINE_string dest_image "vsoc-host-scratch-${USER}" "Image to create" "o"
DEFINE_string dest_family "" "Image family to add the image to" "f"
DEFINE_string dest_project "$(gcloud config get-value project)" \
  "Project to use for the new image" "p"
DEFINE_string launch_instance "" \
  "Name of the instance to launch with the new image" "l"
DEFINE_string source_image_family "debian-10" \
  "Image familty to use as the base" "s"
DEFINE_string source_image_project debian-cloud \
  "Project holding the base image" "m"
DEFINE_string repository_url \
  "https://github.com/google/android-cuttlefish.git" \
  "URL to the repository with host changes" "u"
DEFINE_string repository_branch master \
  "Branch to check out" "b"
DEFINE_string variant master \
  "Variant to build: generally master or stable"


SSH_FLAGS=(${INTERNAL_IP})

fatal_echo() {
  echo "$1"
  exit 1
}

wait_for_instance() {
  alive=""
  while [[ -z "${alive}" ]]; do
    sleep 5
    alive="$(gcloud compute ssh "${SSH_FLAGS[@]}" "$@" -- uptime || true)"
  done
}

package_source() {
  local url="$1"
  local branch="$2"
  local repository_dir="${url/*\//}"
  local debian_dir="$(basename "${repository_dir}" .git)"
  if [[ $# -eq 4 ]]; then
    debian_dir="${repository_dir}/$4"
  fi
  git clone "${url}" -b "${branch}"
  dpkg-source -b "${debian_dir}"
  rm -rf "${debian_dir}"
}

main() {
  set -o errexit
  set -x
  PZ=(--project=${FLAGS_build_project} --zone=${FLAGS_build_zone})
  if [[ -n "${FLAGS_dest_family}" ]]; then
    dest_family_flag=("--family=${FLAGS_dest_family}")
  else
    dest_family_flag=()
  fi
  scratch_dir="$(mktemp -d)"
  pushd "${scratch_dir}"
  package_source "${FLAGS_repository_url}" "${FLAGS_repository_branch}" \
    "cuttlefish-common_${FLAGS_version}"
  popd
  source_files=(
    "${ANDROID_BUILD_TOP}/device/google/cuttlefish/tools/create_base_image_gce.sh"
    ${scratch_dir}/*
  )
  if [[ -n "${INTERNAL_extra_source}" ]]; then
    source_files+=("${INTERNAL_extra_source}"/*)
  fi

  delete_instances=("${FLAGS_build_instance}" "${FLAGS_dest_image}")
  if [[ -n "${FLAGS_launch_instance}" ]]; then
    delete_instances+=("${FLAGS_launch_instance}")
  fi
  gcloud compute instances delete -q \
    "${PZ[@]}" "${delete_instances[@]}" || \
      echo Not running
  gcloud compute disks delete -q \
    "${PZ[@]}" "${FLAGS_dest_image}" || echo No scratch disk
  gcloud compute images delete -q \
    --project="${FLAGS_build_project}" "${FLAGS_dest_image}" || echo Not respinning
  gcloud compute disks create \
    "${PZ[@]}" \
    --image-family="${FLAGS_source_image_family}" \
    --image-project="${FLAGS_source_image_project}" \
    "${FLAGS_dest_image}"
  local gpu_type="nvidia-tesla-p100-vws"
  gcloud compute accelerator-types describe "${gpu_type}" "${PZ[@]}" || \
    fatal_echo "Please use a zone with ${gpu_type} GPUs available."
  gcloud compute instances create \
    "${PZ[@]}" \
    --machine-type=n1-standard-16 \
    --image-family="${FLAGS_source_image_family}" \
    --image-project="${FLAGS_source_image_project}" \
    --boot-disk-size=200GiB \
    --accelerator="type=${gpu_type},count=1" \
    --maintenance-policy=TERMINATE \
    "${FLAGS_build_instance}"
  wait_for_instance "${PZ[@]}" "${FLAGS_build_instance}"
  # Ubuntu tends to mount the wrong disk as root, so help it by waiting until
  # it has booted before giving it access to the clean image disk
  gcloud compute instances attach-disk \
      "${PZ[@]}" \
      "${FLAGS_build_instance}" --disk="${FLAGS_dest_image}"
  # beta for the --internal-ip flag that may be passed via SSH_FLAGS
  gcloud beta compute scp "${SSH_FLAGS[@]}" "${PZ[@]}" \
    "${source_files[@]}" \
    "${FLAGS_build_instance}:"
  gcloud compute ssh "${SSH_FLAGS[@]}" \
    "${PZ[@]}" "${FLAGS_build_instance}" -- \
    ./create_base_image_gce.sh
  gcloud compute instances delete -q \
    "${PZ[@]}" "${FLAGS_build_instance}"
  gcloud compute images create \
    --project="${FLAGS_build_project}" \
    --source-disk="${FLAGS_dest_image}" \
    --source-disk-zone="${FLAGS_build_zone}" \
    --licenses=https://www.googleapis.com/compute/v1/projects/vm-options/global/licenses/enable-vmx \
    "${dest_family_flag[@]}" \
    "${FLAGS_dest_image}"
  gcloud compute disks delete -q "${PZ[@]}" \
    "${FLAGS_dest_image}"
  if [[ -n "${FLAGS_launch_instance}" ]]; then
    gcloud compute instances create "${PZ[@]}" \
      --image-project="${FLAGS_build_project}" \
      --image="${FLAGS_dest_image}" \
      --machine-type=n1-standard-4 \
      --scopes storage-ro \
      --accelerator="type=${gpu_type},count=1" \
      --maintenance-policy=TERMINATE \
      "${FLAGS_launch_instance}"
  fi
  cat <<EOF
    echo Test and if this looks good, consider releasing it via:

    gcloud compute images create \
      --project="${FLAGS_dest_project}" \
      --source-image="${FLAGS_dest_image}" \
      --source-image-project="${FLAGS_build_project}" \
      "${dest_family_flag[@]}" \
      "${FLAGS_dest_image}"
EOF
}
