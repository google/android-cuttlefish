load("orchestration.bzl", "create_single_instance_test")

genrule(
    name = "generate_data",
    srcs = [],  # No input files in this example
    outs = ["output_data.txt"],
    cmd = "date > $(location output_data.txt)",
)

genrule(
    name = "generate_data_2",
    srcs = [],  # No input files in this example
    outs = ["output_data_2.txt"],
    cmd = "$(location :baztool) $@",
    tools = [":baztool"],
)


sh_test(
    name = "foo",
    srcs = ["foo.sh"],
    data = [":generate_data"], 
)

sh_test(
    name = "bar",
    srcs = ["bar.sh"],
    data = [":generate_data"],
)

sh_binary(
    name = "baztool",
    srcs = ["baztool.sh"],
)

create_single_instance_test(
  name="create_aosp_main_x64_phone",
  branch="aosp-main",
  target="aosp_cf_x86_64_phone-trunk_staging-userdebug",
)


