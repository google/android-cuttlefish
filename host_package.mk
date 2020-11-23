cvd_host_packages := $(SOONG_HOST_OUT)/cvd-host_package.tar.gz
ifeq ($(HOST_CROSS_OS)_$(HOST_CROSS_ARCH),linux_bionic_arm64)
  cvd_host_packages += $(SOONG_OUT_DIR)/host/$(HOST_CROSS_OS)-$(HOST_CROSS_ARCH)/cvd-host_package.tar.gz
endif

.PHONY: hosttar
hosttar: $(cvd_host_packages)

# Build this by default when a developer types make
droidcore: $(cvd_host_packages)

# Dist
$(call dist-for-goals, dist_files,$(cvd_host_packages))

cvd_host_packages :=
