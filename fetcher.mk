bin_path := $(HOST_OUT_EXECUTABLES)
ifeq ($(HOST_CROSS_OS), linux_musl)
  bin_path := $(OUT_DIR)/host/$(HOST_CROSS_OS)-$(HOST_CROSS_ARCH)/bin
endif

cvd_bin := $(bin_path)/cvd
fetcher_bin := $(bin_path)/fetch_cvd

.PHONY: host_fetcher
host_fetcher: $(fetcher_bin)

# Build this by default when a developer types make
droidcore: $(cvd_bin) $(fetcher_bin)

# Build and store them on the build server.
$(call dist-for-goals, dist_files, $(cvd_bin))
$(call dist-for-goals, dist_files, $(fetcher_bin))

bin_path :=
cvd_bin :=
fetcher_bin :=
