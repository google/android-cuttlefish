#!/usr/bin/sh
set -e

DEB_HOST_MULTIARCH=`dpkg-architecture -qDEB_HOST_MULTIARCH`
HOME1=`mktemp -d`
CACHE1=`mktemp -d`
MOCKREPOS=`mktemp -d`
python3 `pwd`/tools/testutils/bazel-7-readiness/generate_mock_repos.py ${MOCKREPOS}

echo "#WORKSPACE" > base/cvd/WORKSPACE
mkdir -p ${MOCKREPOS}/googleapis/google/rpc
ln -sf /usr/lib/python3/dist-packages/google/rpc/status.proto ${MOCKREPOS}/googleapis/google/rpc/status.proto
ln -sf /usr/lib/python3/dist-packages/google/rpc/code.proto ${MOCKREPOS}/googleapis/google/rpc/code.proto

OVERRIDE_ARGS=`cat ${MOCKREPOS}/override_args.txt`

cd base/cvd
trap "env HOME=${HOME1} bazel shutdown || true" EXIT

env HOME=${HOME1} bazel build \
  --nobuild --linkopt="-Wl,--build-id=sha1" --spawn_strategy=local \
  --verbose_failures \
  --sandbox_debug \
  --repository_cache=${CACHE1} \
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
  --registry=file://${MOCKREPOS} \
  --check_direct_dependencies=off \
  --override_module=rules_shell=/usr/share/bazel/rules/shell \
  ${OVERRIDE_ARGS} \
  cuttlefish/host/commands/cvd:cvd \
  cuttlefish/host/commands/cvdalloc:cvdalloc \
  cuttlefish/host/commands/refresh_groups:cvd_refresh_groups \
  cuttlefish/host/commands/defaults:cf_defaults \
  cuttlefish/host/commands/assemble_cvd:assemble_cvd \
  cuttlefish/host/commands/start:cvd_internal_start \
  cuttlefish/host/commands/run_cvd:run_cvd \
  cuttlefish/host/commands/stop:cvd_internal_stop \
  cuttlefish/host/commands/status:cvd_internal_status \
  cuttlefish/host/commands/display:cvd_internal_display \
  cuttlefish/host/commands/host_bugreport:cvd_internal_host_bugreport
