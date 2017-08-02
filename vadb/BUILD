cc_binary(
    name = "vadb",
    srcs = [
        "main.cpp",
        "usb_cmd.h",
        "usb_cmd_attach.cpp",
        "usb_cmd_attach.h",
        "usb_cmd_control_transfer.cpp",
        "usb_cmd_control_transfer.h",
        "usb_cmd_data_transfer.cpp",
        "usb_cmd_data_transfer.h",
        "usb_cmd_device_list.cpp",
        "usb_cmd_device_list.h",
        "virtual_adb.cpp",
        "virtual_adb.h",
    ],
    deps = [
        "@cuttlefish//common/libs/fs",
        "@cuttlefish//guest/usbforward:protocol",
        "@gflags_repo//:gflags",
        "@glog_repo//:glog",
        "//host/vadb/usbip:usbip_lib",
    ],
)