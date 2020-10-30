soong_host_out := $(SOONG_HOST_OUT)
ifeq ($(HOST_CROSS_OS)_$(HOST_CROSS_ARCH),linux_bionic_arm64)
  soong_host_out := $(SOONG_OUT_DIR)/host/$(HOST_CROSS_OS)-$(HOST_CROSS_ARCH)
endif
cvd_host_package_tar := $(soong_host_out)/cvd-host_package.tar.gz

.PHONY: hosttar
hosttar: $(cvd_host_package_tar)

# Build this by default when a developer types make
droidcore: $(cvd_host_package_tar)

# Dist
$(call dist-for-goals, dist_files,$(cvd_host_package_tar))

cvd_host_package_tar :=
soong_host_out :=
