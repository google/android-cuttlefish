load("//guest:ramdisk.bzl", "ramdisk_files", "ramdisk_create")

ramdisk_create(
    name = "gce_ramdisk",
    out = "gce_ramdisk.img",
    srcs = [
        ":ramdisk_bin_files",
        ":ramdisk_root_files",
    ],
)

ramdisk_files(
    name = "ramdisk_root_files",
    srcs = [
        "//guest/gce_init:init",
    ],
    out = "/",
)

ramdisk_files(
    name = "ramdisk_bin_files",
    srcs = [
        "//guest/gce_network:meta_test",
        "//guest/gce_init:gce_init_dhcp_hook",
    ],
    out = "/bin",
)
