#!/bin/sh
set -e

REPO_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
DEB_HOST_MULTIARCH=$(dpkg-architecture -qDEB_HOST_MULTIARCH)
WORKDIR=$(mktemp -d)
HOME1="${WORKDIR}/home"
CACHE1="${WORKDIR}/cache"
MOCKREPOS="${WORKDIR}/mock_repos"
CVD_WORK="${WORKDIR}/cvd"

cleanup() {
  if [ -d "${CVD_WORK}" ]; then
    (cd "${CVD_WORK}" && env HOME="${HOME1}" bazel shutdown >/dev/null 2>&1 || true)
  fi
  rm -rf "${WORKDIR}"
}
trap cleanup EXIT

mkdir -p "${HOME1}" "${CACHE1}" "${MOCKREPOS}" "${CVD_WORK}"
tar -C "${REPO_ROOT}/base/cvd" --exclude='bazel-*' -cf - . | tar -C "${CVD_WORK}" -xf -

python3 "${REPO_ROOT}/tools/testutils/bazel-7-readiness/generate_mock_repos.py" "${MOCKREPOS}" "${CVD_WORK}"

echo "#WORKSPACE" > "${CVD_WORK}/WORKSPACE"
mkdir -p "${MOCKREPOS}/googleapis/google/rpc"
ln -sf /usr/lib/python3/dist-packages/google/rpc/status.proto "${MOCKREPOS}/googleapis/google/rpc/status.proto"
ln -sf /usr/lib/python3/dist-packages/google/rpc/code.proto "${MOCKREPOS}/googleapis/google/rpc/code.proto"

OVERRIDE_ARGS=$(cat "${MOCKREPOS}/override_args.txt")
SYS_RULE_OVERRIDES=""
for rule in shell license proto; do
  if [ -d "/usr/share/bazel/rules/${rule}" ]; then
    SYS_RULE_OVERRIDES="${SYS_RULE_OVERRIDES} --override_module=rules_${rule}=/usr/share/bazel/rules/${rule}"
  fi
done

cd "${CVD_WORK}"
env HOME="${HOME1}" bazel build \
  --nobuild \
  --linkopt="-Wl,--build-id=sha1" \
  --spawn_strategy=local \
  --verbose_failures \
  --sandbox_debug \
  --repository_cache="${CACHE1}" \
  --strip=never \
  --copt="-g" \
  --copt="-isystem/usr/include/jsoncpp" \
  --copt="-isystem/usr/include/libxml2" \
  --copt="-isystem/usr/include/freetype2" \
  --copt="-isystem/usr/include/libpng16" \
  --copt="-isystem/usr/include/android" \
  --copt="-isystem/usr/include/libdrm" \
  --linkopt="-L/usr/lib/${DEB_HOST_MULTIARCH}/android" \
  --linkopt="-Wl,-rpath,/usr/lib/${DEB_HOST_MULTIARCH}/android" \
  --linkopt="-z muldefs" \
  --noenable_workspace \
  --registry="file://${MOCKREPOS}" \
  --check_direct_dependencies=off \
  ${SYS_RULE_OVERRIDES} \
  ${OVERRIDE_ARGS} \
  //cuttlefish/package:cvd \
  //cuttlefish/package:defaults \
  //cuttlefish/package:metrics
