cvd_host_packages := $(HOST_OUT)/cvd-host_package.tar.gz
ifeq ($(HOST_CROSS_OS)_$(HOST_CROSS_ARCH),linux_bionic_arm64)
  cvd_host_packages += $(OUT_DIR)/host/$(HOST_CROSS_OS)-$(HOST_CROSS_ARCH)/cvd-host_package.tar.gz
endif

.PHONY: hosttar
hosttar: $(cvd_host_packages)

# Build this by default when a developer types make
droidcore: $(cvd_host_packages)

# Dist
# Note that only the last package is dist'ed. It would be from x86 in case of cf_x86_phone,
# and from arm64 in case of cf_arm64_phone.
$(call dist-for-goals, dist_files,$(word $(words $(cvd_host_packages)), $(cvd_host_packages)))

cvd_host_packages :=
