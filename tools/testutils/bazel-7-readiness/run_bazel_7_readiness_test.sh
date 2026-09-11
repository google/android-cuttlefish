#!/usr/bin/sh
set -e

DEB_HOST_MULTIARCH=`dpkg-architecture -qDEB_HOST_MULTIARCH`
HOME1=`mktemp -d`
CACHE1=`mktemp -d`
MOCKREPOS=`mktemp -d`
python3 `pwd`/tools/testutils/bazel-7-readiness/generate_mock_repos.py ${MOCKREPOS}

for i in `pwd`/tools/testutils/bazel-7-readiness/patches/*.patch; do
    patch -p1 < $i
done

echo "#WORKSPACE" > base/cvd/WORKSPACE
mkdir -p ${MOCKREPOS}/googleapis/google/rpc
ln -sf /usr/lib/python3/dist-packages/google/rpc/status.proto ${MOCKREPOS}/googleapis/google/rpc/status.proto
ln -sf /usr/lib/python3/dist-packages/google/rpc/code.proto ${MOCKREPOS}/googleapis/google/rpc/code.proto

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
  --override_module=abseil-cpp=${MOCKREPOS}/abseil-cpp \
  --override_module=apple_support=${MOCKREPOS}/apple_support \
  --override_module=aspect_bazel_lib=${MOCKREPOS}/aspect_bazel_lib \
  --override_module=aspect_rules_lint=${MOCKREPOS}/aspect_rules_lint \
  --override_module=boringssl=${MOCKREPOS}/boringssl \
  --override_module=brotli=${MOCKREPOS}/brotli \
  --override_module=buildozer=${MOCKREPOS}/buildozer \
  --override_module=bzip2=${MOCKREPOS}/bzip2 \
  --override_module=c-ares=${MOCKREPOS}/c-ares \
  --override_module=crc32c=${MOCKREPOS}/crc32c \
  --override_module=curl=${MOCKREPOS}/curl \
  --override_module=cxx.rs=${MOCKREPOS}/cxx.rs \
  --override_module=fmt=${MOCKREPOS}/fmt \
  --override_module=freetype=${MOCKREPOS}/freetype \
  --override_module=gflags=${MOCKREPOS}/gflags \
  --override_module=googleapis=${MOCKREPOS}/googleapis \
  --override_module=googleapis-cc=${MOCKREPOS}/googleapis-cc \
  --override_module=googletest=${MOCKREPOS}/googletest \
  --override_module=grpc=${MOCKREPOS}/grpc \
  --override_module=grpc-proto=${MOCKREPOS}/grpc-proto \
  --override_module=hedron_compile_commands=${MOCKREPOS}/hedron_compile_commands \
  --override_module=icu=${MOCKREPOS}/icu \
  --override_module=jsoncpp=${MOCKREPOS}/jsoncpp \
  --override_module=libarchive=${MOCKREPOS}/libarchive \
  --override_module=libevent=${MOCKREPOS}/libevent \
  --override_module=libjpeg_turbo=${MOCKREPOS}/libjpeg_turbo \
  --override_module=libpng=${MOCKREPOS}/libpng \
  --override_module=libuuid=${MOCKREPOS}/libuuid \
  --override_module=libxml2=${MOCKREPOS}/libxml2 \
  --override_module=libzip=${MOCKREPOS}/libzip \
  --override_module=lz4=${MOCKREPOS}/lz4 \
  --override_module=nasm=${MOCKREPOS}/nasm \
  --override_module=pcre2=${MOCKREPOS}/pcre2 \
  --override_module=protobuf=${MOCKREPOS}/protobuf \
  --override_module=re2=${MOCKREPOS}/re2 \
  --override_module=rootcanal=${MOCKREPOS}/rootcanal \
  --override_module=rules_flex=${MOCKREPOS}/rules_flex \
  --override_module=rules_foreign_cc=${MOCKREPOS}/rules_foreign_cc \
  --override_module=rules_m4=${MOCKREPOS}/rules_m4 \
  --override_module=rules_nodejs=${MOCKREPOS}/rules_nodejs \
  --override_module=rules_proto_grpc_cpp=${MOCKREPOS}/rules_proto_grpc_cpp \
  --override_module=rules_proto_grpc_go=${MOCKREPOS}/rules_proto_grpc_go \
  --override_module=rules_rust=${MOCKREPOS}/rules_rust \
  --override_module=rules_shell=/usr/share/bazel/rules/shell \
  --override_repository=_main~_repo_rules~android_system_core=${MOCKREPOS}/android_system_core \
  --override_repository=android_system_core=${MOCKREPOS}/android_system_core \
  --override_repository=_main~_repo_rules~android_system_extras=${MOCKREPOS}/android_system_extras \
  --override_repository=android_system_extras=${MOCKREPOS}/android_system_extras \
  --override_module=android_tools_netsim=${MOCKREPOS}/android_tools_netsim \
  --override_repository=_main~_repo_rules~arm_optimized_routines=${MOCKREPOS}/arm_optimized_routines \
  --override_repository=arm_optimized_routines=${MOCKREPOS}/arm_optimized_routines \
  --override_repository=_main~_repo_rules~avb=${MOCKREPOS}/avb \
  --override_repository=avb=${MOCKREPOS}/avb \
  --override_repository=_main~_repo_rules~casimir=${MOCKREPOS}/casimir \
  --override_repository=casimir=${MOCKREPOS}/casimir \
  --override_module=crosvm=${MOCKREPOS}/crosvm \
  --override_module=dosfstools=${MOCKREPOS}/dosfstools \
  --override_module=e2fsprogs=${MOCKREPOS}/e2fsprogs \
  --override_repository=_main~_repo_rules~egl_headers=${MOCKREPOS}/egl_headers \
  --override_repository=egl_headers=${MOCKREPOS}/egl_headers \
  --override_repository=_main~_repo_rules~expat=${MOCKREPOS}/expat \
  --override_repository=expat=${MOCKREPOS}/expat \
  --override_repository=_main~_repo_rules~f2fs_tools=${MOCKREPOS}/f2fs_tools \
  --override_repository=f2fs_tools=${MOCKREPOS}/f2fs_tools \
  --override_repository=_main~_repo_rules~fec_rs=${MOCKREPOS}/fec_rs \
  --override_repository=fec_rs=${MOCKREPOS}/fec_rs \
  --override_module=fruit=${MOCKREPOS}/fruit \
  --override_module=gfxstream=${MOCKREPOS}/gfxstream \
  --override_module=libcbor=${MOCKREPOS}/libcbor \
  --override_repository=_main~_repo_rules~libconfig=${MOCKREPOS}/libconfig \
  --override_repository=libconfig=${MOCKREPOS}/libconfig \
  --override_repository=_main~_repo_rules~libdrm=${MOCKREPOS}/libdrm \
  --override_repository=libdrm=${MOCKREPOS}/libdrm \
  --override_repository=_main~_repo_rules~libeigen=${MOCKREPOS}/libeigen \
  --override_repository=libeigen=${MOCKREPOS}/libeigen \
  --override_module=libffi=${MOCKREPOS}/libffi \
  --override_repository=_main~_repo_rules~libnl=${MOCKREPOS}/libnl \
  --override_repository=libnl=${MOCKREPOS}/libnl \
  --override_module=libopenscreen=${MOCKREPOS}/libopenscreen \
  --override_repository=_main~_repo_rules~libpffft=${MOCKREPOS}/libpffft \
  --override_repository=libpffft=${MOCKREPOS}/libpffft \
  --override_module=libsrtp2=${MOCKREPOS}/libsrtp2 \
  --override_repository=_main~_repo_rules~libusb=${MOCKREPOS}/libusb \
  --override_repository=libusb=${MOCKREPOS}/libusb \
  --override_module=libvpx=${MOCKREPOS}/libvpx \
  --override_repository=_main~_repo_rules~libwebm=${MOCKREPOS}/libwebm \
  --override_repository=libwebm=${MOCKREPOS}/libwebm \
  --override_repository=_main~_repo_rules~libwebrtc=${MOCKREPOS}/libwebrtc \
  --override_repository=libwebrtc=${MOCKREPOS}/libwebrtc \
  --override_module=libwebsockets=${MOCKREPOS}/libwebsockets \
  --override_repository=_main~_repo_rules~libyuv=${MOCKREPOS}/libyuv \
  --override_repository=libyuv=${MOCKREPOS}/libyuv \
  --override_repository=_main~_repo_rules~mako=${MOCKREPOS}/mako \
  --override_repository=mako=${MOCKREPOS}/mako \
  --override_repository=_main~_repo_rules~markupsafe=${MOCKREPOS}/markupsafe \
  --override_repository=markupsafe=${MOCKREPOS}/markupsafe \
  --override_repository=_main~_repo_rules~mesa=${MOCKREPOS}/mesa \
  --override_repository=mesa=${MOCKREPOS}/mesa \
  --override_repository=_main~_repo_rules~mkbootimg=${MOCKREPOS}/mkbootimg \
  --override_repository=mkbootimg=${MOCKREPOS}/mkbootimg \
  --override_module=ms-tpm-20-ref=${MOCKREPOS}/ms-tpm-20-ref \
  --override_module=mtools=${MOCKREPOS}/mtools \
  --override_repository=_main~_repo_rules~opengl_headers=${MOCKREPOS}/opengl_headers \
  --override_repository=opengl_headers=${MOCKREPOS}/opengl_headers \
  --override_repository=_main~_repo_rules~pyyaml=${MOCKREPOS}/pyyaml \
  --override_repository=pyyaml=${MOCKREPOS}/pyyaml \
  --override_repository=_main~_repo_rules~selinux=${MOCKREPOS}/selinux \
  --override_repository=selinux=${MOCKREPOS}/selinux \
  --override_module=spirv_headers=${MOCKREPOS}/spirv_headers \
  --override_module=spirv_tools=${MOCKREPOS}/spirv_tools \
  --override_module=swiftshader=${MOCKREPOS}/swiftshader \
  --override_repository=_main~_repo_rules~vulkan_headers=${MOCKREPOS}/vulkan_headers \
  --override_repository=vulkan_headers=${MOCKREPOS}/vulkan_headers \
  --override_module=wayland=${MOCKREPOS}/wayland \
  --override_repository=_main~_repo_rules~wmediumd=${MOCKREPOS}/wmediumd \
  --override_repository=wmediumd=${MOCKREPOS}/wmediumd \
  --override_module=sandboxed_api=${MOCKREPOS}/sandboxed_api \
  --override_module=tinyxml2=${MOCKREPOS}/tinyxml2 \
  --override_module=tl-expected=${MOCKREPOS}/tl-expected \
  --override_module=toolchains_llvm=${MOCKREPOS}/toolchains_llvm \
  --override_module=xz=${MOCKREPOS}/xz \
  --override_module=zlib=${MOCKREPOS}/zlib \
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
