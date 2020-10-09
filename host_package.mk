LOCAL_PATH := $(call my-dir)

cvd_host_package_tar := $(HOST_OUT)/cvd-host_package.tar.gz

.PHONY: hosttar
hosttar: $(cvd_host_package_tar)

# Build this by default when a developer types make
droidcore: $(cvd_host_package_tar)

# Build and store them on the build server.
$(call dist-for-goals, dist_files, $(cvd_host_package_tar))

bin_path := $(notdir $(HOST_OUT_EXECUTABLES))
lib_path := $(notdir $(HOST_OUT_SHARED_LIBRARIES))
tests_path := $(notdir $(HOST_OUT_NATIVE_TESTS))
webrtc_files_path := usr/share/webrtc
modem_simulator_path := etc/modem_simulator

cvd_host_executables := \
    aarch64-linux-gnu/crosvm \
    aarch64-linux-gnu/libepoxy.so.0 \
    aarch64-linux-gnu/libgbm.so.1 \
    aarch64-linux-gnu/libminijail.so \
    aarch64-linux-gnu/libvirglrenderer.so.1 \
    adb \
    adb_connector \
    adbshell \
    allocd \
    allocd_client \
    assemble_cvd \
    config_server \
    console_forwarder \
    crosvm \
    cvd_status \
    detect_graphics \
    extract-vmlinux \
    fsck.f2fs \
    gnss_grpc_proxy \
    kernel_log_monitor \
    launch_cvd \
    log_tee \
    logcat_receiver \
    lpmake \
    lpunpack \
    lz4 \
    make_f2fs \
    metrics \
    mkenvimage \
    modem_simulator \
    ms-tpm-20-ref \
    newfs_msdos \
    powerwash_cvd \
    resize.f2fs \
    run_cvd \
    secure_env \
    socket_vsock_proxy \
    stop_cvd \
    tapsetiff \
    tombstone_receiver \
    vnc_server \
    webRTC \
    webrtc_operator \
    x86_64-linux-gnu/crosvm \
    x86_64-linux-gnu/libOpenglRender.so \
    x86_64-linux-gnu/libandroid-emu-shared.so \
    x86_64-linux-gnu/libc++.so.1 \
    x86_64-linux-gnu/libemugl_common.so \
    x86_64-linux-gnu/libgfxstream_backend.so \

ifneq ($(wildcard device/google/trout),)
    cvd_host_executables += android.hardware.automotive.vehicle@2.0-virtualization-grpc-server
endif

cvd_host_tests := \
    cuttlefish_net_tests \
    modem_simulator_test \

cvd_host_shared_libraries := \
    android.hardware.automotive.vehicle@2.0.so \
    cuttlefish_net.so \
    libandroidicu-host.so \
    libbase.so \
    libc++.so \
    libcap.so \
    libcrypto-host.so \
    libcrypto_utils.so \
    libcutils.so \
    libcuttlefish_allocd_utils.so \
    libcuttlefish_device_config.so \
    libcuttlefish_fs.so \
    libcuttlefish_kernel_log_monitor_utils.so \
    libcuttlefish_security.so \
    libcuttlefish_utils.so \
    libdrm.so \
    libepoxy.so \
    libext4_utils.so \
    libfdt.so \
    libgatekeeper.so \
    libgbm.so \
    libgrpc++.so \
    libhidlbase.so \
    libicui18n-host.so \
    libicuuc-host.so \
    libjpeg.so \
    libjsoncpp.so \
    libkeymaster_messages.so \
    libkeymaster_portable.so \
    liblog.so \
    liblp.so \
    libminijail.so \
    libnl.so \
    libopus.so \
    libprotobuf-cpp-full.so \
    libpuresoftkeymasterdevice_host.so \
    libsoft_attestation_cert.so \
    libsparse-host.so \
    libssl-host.so \
    libutils.so \
    libvirglrenderer.so \
    libvpx.so \
    libwayland_client.so \
    libxml2.so \
    libyuv.so \
    libz-host.so \
    libziparchive.so \
    ms-tpm-20-ref-lib.so \
    tpm2-tss2-esys.so \
    tpm2-tss2-mu.so \
    tpm2-tss2-rc.so \
    tpm2-tss2-sys.so \
    tpm2-tss2-tcti.so \
    tpm2-tss2-util.so \

webrtc_assets := \
    index.html \
    js/adb.js \
    js/app.js \
    js/cf_webrtc.js \
    style.css \

webrtc_certs := \
    server.crt \
    server.key \
    server.p12 \
    trusted.pem \

cvd_host_webrtc_files := \
    $(addprefix assets/,$(webrtc_assets)) \
    $(addprefix certs/,$(webrtc_certs)) \

modem_simulator_files := \
     iccprofile_for_sim0.xml \
     iccprofile_for_sim0_for_CtsCarrierApiTestCases.xml \
     numeric_operator.xml \

include external/crosvm/seccomp/host_package.mk

cvd_host_package_files := \
     $(addprefix $(bin_path)/,$(cvd_host_executables)) \
     $(addprefix $(lib_path)/,$(cvd_host_shared_libraries)) \
     $(foreach test,$(cvd_host_tests), ${tests_path}/$(test)/$(test)) \
     $(addprefix $(webrtc_files_path)/,$(cvd_host_webrtc_files)) \
     $(addprefix $(modem_simulator_path)/files/,$(modem_simulator_files)) \
     $(crosvm_inline_seccomp_policy_x86_64) \
     $(crosvm_inline_seccomp_policy_aarch64) \

$(cvd_host_package_tar): PRIVATE_FILES := $(cvd_host_package_files)
$(cvd_host_package_tar): $(addprefix $(HOST_OUT)/,$(cvd_host_package_files))
	$(hide) rm -rf $@ && tar Scfzh $@.tmp -C $(HOST_OUT) $(PRIVATE_FILES)
	$(hide) mv $@.tmp $@
