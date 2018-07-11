#!/bin/bash

# Common code to build a host image on GCE

# INTERNAL_extra_source may be set to a directory containing the source for
# extra package to build.

source "${ANDROID_BUILD_TOP}/external/shflags/src/shflags"

DEFINE_string build_instance \
  "${USER}-build" "Instance name to create for the build" "i"
DEFINE_string dest_image "vsoc-host-scratch-${USER}" "Image to create" "o"
DEFINE_string dest_family "" "Image family to add the image to" "f"
DEFINE_string dest_project "" "Project to use for the new image" "p"
DEFINE_string launch_instance "" \
  "Name of the instance to launch with the new image" "l"
DEFINE_string source_image_family debian-9 "Image familty to use as the base" \
  "s"
DEFINE_string source_image_project debian-cloud \
  "Project holding the base image" "m"
DEFINE_string repository_url \
  https://github.com/google/android-cuttlefish.git \
  "URL to the repository with host changes" "u"
DEFINE_string repository_branch master \
  "Branch to check out" "b"

wait_for_instance() {
  alive=""
  while [[ -z "${alive}" ]]; do
    sleep 5
    alive="$(gcloud compute ssh "$@" -- uptime || true)"
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
  if [[ -n "${FLAGS_dest_project}" ]]; then
    dest_project_flag=("--project=${FLAGS_dest_project}")
  else
    dest_project_flag=()
  fi
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
  if [[ -n "${INTERNAL_extra_source}" ]]; then
    source_files=("${INTERNAL_extra_source}"/* ${scratch_dir}/*)
  else
    source_files=(${scratch_dir}/*)
  fi

  delete_instances=("${FLAGS_build_instance}" "${FLAGS_dest_image}")
  if [[ -n "${FLAGS_launch_instance}" ]]; then
    delete_instances+=("${FLAGS_launch_instance}")
  fi
  gcloud compute instances delete -q \
    "${dest_project_flag[@]}" "${delete_instances[@]}" || \
      echo Not running
  gcloud compute disks delete -q \
    "${dest_project_flag[@]}" "${FLAGS_dest_image}" || echo No scratch disk
  gcloud compute images delete -q \
    "${dest_project_flag[@]}" "${FLAGS_dest_image}" || echo Not respinning
  gcloud compute disks create \
    "${dest_project_flag[@]}" \
    --image-family="${FLAGS_source_image_family}" \
    --image-project="${FLAGS_source_image_project}" \
    "${FLAGS_dest_image}"
  gcloud compute instances create \
    "${dest_project_flag[@]}" \
    --image-family="${FLAGS_source_image_family}" \
    --image-project="${FLAGS_source_image_project}" \
    "${FLAGS_build_instance}"
  wait_for_instance "${dest_project_flag[@]}" "${FLAGS_build_instance}"
  # Ubuntu tends to mount the wrong disk as root, so help it by waiting until
  # it has booted before giving it access to the clean image disk
  gcloud compute instances attach-disk \
      "${dest_project_flag[@]}" \
      "${FLAGS_build_instance}" --disk="${FLAGS_dest_image}"
  gcloud compute scp "${dest_project_flag[@]}" \
    "${source_files[@]}" \
    "${ANDROID_BUILD_TOP}/device/google/cuttlefish_common/tools/create_base_image_gce.sh" \
    "${FLAGS_build_instance}:"
  gcloud compute ssh \
    "${dest_project_flag[@]}" "${FLAGS_build_instance}" -- \
    ./create_base_image_gce.sh
  gcloud compute instances delete -q \
    "${dest_project_flag[@]}" "${FLAGS_build_instance}"
  gcloud compute images create "${dest_project_flag[@]}" \
    --source-disk="${FLAGS_dest_image}" \
    --licenses=https://www.googleapis.com/compute/v1/projects/vm-options/global/licenses/enable-vmx \
    "${dest_family_flag[@]}" \
    "${FLAGS_dest_image}"
  gcloud compute disks delete -q "${dest_project_flag[@]}" \
    "${FLAGS_dest_image}"
  if [[ -n "${FLAGS_launch_instance}" ]]; then
    gcloud compute instances create "${dest_project_flag[@]}" \
      --image="${FLAGS_dest_image}" \
      --machine-type=n1-standard-2 \
      --scopes storage-ro \
      "${FLAGS_launch_instance}"
  fi
}
