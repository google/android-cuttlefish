LOCAL_PATH := $(call my-dir)

cvd_host_package_tar := $(HOST_OUT)/cvd-host_package.tar.gz

.PHONY: hosttar
hosttar: $(cvd_host_package_tar)

.PHONY: cf_local_image
cf_local_image: bootimage cacheimage hosttar systemimage userdataimage vendorimage

$(cvd_host_package_tar): PRIVATE_TAR_FORMAT :=
ifeq ($(HOST_OS),linux)
$(cvd_host_package_tar): PRIVATE_TAR_FORMAT := --format=gnu
endif

# Build and store them on the build server.
$(call dist-for-goals, dist_files, $(cvd_host_package_tar))

bin_path := $(notdir $(HOST_OUT_EXECUTABLES))
lib_path := $(notdir $(HOST_OUT_SHARED_LIBRARIES))
tests_path := $(notdir $(HOST_OUT_NATIVE_TESTS))

cvd_host_executables := \
    adbshell \
    host_region_e2e_test \
    launch_cvd \
    socket_forward_proxy \
    adb_connector \
    stop_cvd \
    vnc_server \
    record_audio \
    cf_qemu.sh \
    ivserver \
    virtual_usb_manager \
    kernel_log_monitor \

cvd_host_tests := \
    auto_free_buffer_test \
    circqueue_test \
    cuttlefish_thread_test \
    hald_client_test \
    lock_test \
    monotonic_time_test \
    vsoc_graphics_test \
    cuttlefish_net_tests \

cvd_host_shared_libraries := \
    libbase \
    vsoc_lib \
    libcuttlefish_fs \
    cuttlefish_auto_resources \
    libcuttlefish_strings \
    libcuttlefish_utils \
    cuttlefish_tcp_socket \
    liblog \
    libnl \
    libc++ \
    libicuuc-host \

cvd_host_configs := \
    system-root.dtb \
    initrd-root.dtb

cvd_host_package_files := \
     $(addprefix config/,$(cvd_host_configs)) \
     $(addprefix $(bin_path)/,$(cvd_host_executables)) \
     $(addprefix $(lib_path)/,$(addsuffix .so,$(cvd_host_shared_libraries))) \
     $(foreach test,$(cvd_host_tests), ${tests_path}/$(test)/$(test)) \

$(cvd_host_package_tar): PRIVATE_FILES := $(cvd_host_package_files)
$(cvd_host_package_tar): $(addprefix $(HOST_OUT)/,$(cvd_host_package_files))
	$(hide) rm -rf $@ && tar Scfz $@.tmp -C $(HOST_OUT) $(PRIVATE_TAR_FORMAT) $(PRIVATE_FILES)
	$(hide) mv $@.tmp $@
