cvd_host_packages := $(HOST_OUT)/cvd-host_package
ifeq ($(HOST_CROSS_OS), linux_musl)
  cvd_host_packages := $(OUT_DIR)/host/$(HOST_CROSS_OS)-$(HOST_CROSS_ARCH)/cvd-host_package $(cvd_host_packages)
endif

cvd_host_dir_stamps := $(addsuffix .stamp,$(cvd_host_packages))

# Build this by default when a developer types make.
# Skip the tarballs by default as it is time consuming.
droidcore: $(cvd_host_dir_stamps)

cvd_host_dir_stamps :=
cvd_host_packages :=
cvd_host_tarballs :=
