cvd_host_packages := $(HOST_OUT)/cvd-host_package
ifeq ($(HOST_CROSS_OS), linux_musl)
  cvd_host_packages := $(OUT_DIR)/host/$(HOST_CROSS_OS)-$(HOST_CROSS_ARCH)/cvd-host_package $(cvd_host_packages)
endif

cvd_host_dir_stamps := $(addsuffix .stamp,$(cvd_host_packages))
cvd_host_tarballs := $(addsuffix .tar.gz,$(cvd_host_packages))

.PHONY: hosttar
hosttar: $(cvd_host_tarballs)

# Build this by default when a developer types make.
# Skip the tarballs by default as it is time consuming.
droidcore: $(cvd_host_dir_stamps)

# Dist
# Dist the first package as cvd-host_package.tar.gz.  It would be from x86 in case of cf_x86_phone,
# and from arm64 in case of cf_arm64_phone.
$(call dist-for-goals, dist_files,$(firstword $(cvd_host_tarballs)))
ifeq ($(HOST_CROSS_OS)-$(HOST_CROSS_ARCH), linux_musl-arm64)
  # If the normal package is arm64, also dist an x86_64 package.
  $(call dist-for-goals, dist_files,$(lastword $(cvd_host_tarballs)):cvd-host_package-x86_64.tar.gz)
endif


cvd_host_dir_stamps :=
cvd_host_packages :=
cvd_host_tarballs :=
