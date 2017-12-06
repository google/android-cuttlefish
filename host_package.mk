LOCAL_PATH := $(call my-dir)

cvd_host_package_tar := $(HOST_OUT)/cvd-host_package.tar.gz

ifeq ($(HOST_OS),linux)
CVD_TAR_FORMAT := --format=gnu
endif

# Build and store them on the build server.
$(call dist-for-goals, dist_files, $(cvd_host_package_tar))

bin_path := $(notdir $(HOST_OUT_EXECUTABLES))
lib_path := $(notdir $(HOST_OUT_SHARED_LIBRARIES))
tests_path := $(notdir $(HOST_OUT_NATIVE_TESTS))

cvd_host_executables := \
    launch_cvd \
    wificlient \

cvd_host_tests := \
    auto_free_buffer_test \
    circqueue_test \
    cuttlefish_thread_test \
    hald_client_test \
    lock_test \
    monotonic_time_test \
    vsoc_graphics_test \

cvd_host_shared_libraries := \
    libbase \
    vsoc_lib \
    libcuttlefish_fs \
    cuttlefish_auto_resources \
    liblog \
    libnl \
    libc++ \
    libicuuc-host \

cvd_host_configs := \
    vsoc_mem.json

cvd_host_packages := \
    vsoc_mem_json \
    $(cvd_host_executables) \
    $(cvd_host_tests) \

cvd_host_package_files := \
     $(addprefix config/,$(cvd_host_configs)) \
     $(addprefix $(bin_path)/,$(cvd_host_executables)) \
     $(addprefix $(lib_path)/,$(addsuffix .so,$(cvd_host_shared_libraries))) \
     $(foreach test,$(cvd_host_tests), ${tests_path}/$(test)/$(test)) \

$(cvd_host_package_tar): $(cvd_host_packages)
	$(hide) rm -rf $@ && tar Scfz $@.tmp -C $(HOST_OUT) $(CVD_TAR_FORMAT) $(cvd_host_package_files)
	$(hide) mv $@.tmp $@
