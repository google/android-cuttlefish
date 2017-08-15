cc_library(
    name = "vadb_lib",
    srcs = [
        "usb_cmd.h",
        "usb_cmd_attach.cpp",
        "usb_cmd_attach.h",
        "usb_cmd_control_transfer.cpp",
        "usb_cmd_control_transfer.h",
        "usb_cmd_data_transfer.cpp",
        "usb_cmd_data_transfer.h",
        "usb_cmd_device_list.cpp",
        "usb_cmd_device_list.h",
        "usb_cmd_heartbeat.cpp",
        "usb_cmd_heartbeat.h",
        "virtual_adb_client.cpp",
        "virtual_adb_client.h",
        "virtual_adb_server.cpp",
        "virtual_adb_server.h",
    ],
    visibility = ["//visibility:public"],
    linkopts = [
        "-ludev",
    ],
    deps = [
        "@cuttlefish//common/libs/fs",
        "@cuttlefish//guest/usbforward:protocol",
        "@gflags_repo//:gflags",
        "@glog_repo//:glog",
        "//host/vadb/usbip:usbip_lib",
    ],
)

