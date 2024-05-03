load("boot_tests.bzl", "launch_cvd_boot_test", "cvd_load_boot_test")

# cvd fetch + launch_cvd tests
launch_cvd_boot_test(
  name="fetch_and_launch_aosp_main_x64_phone",
  branch="aosp-main",
  target="aosp_cf_x86_64_phone-trunk_staging-userdebug"
)

launch_cvd_boot_test(
  name="fetch_and_launch_aosp_android14_phone",
  branch="aosp-android14-gsi",
  target="aosp_cf_x86_64_phone-userdebug"
)

launch_cvd_boot_test(
  name="fetch_and_launch_aosp_android13_phone",
  branch="aosp-android13-gsi",
  target="aosp_cf_x86_64_phone-userdebug"
)

launch_cvd_boot_test(
  name="fetch_and_launch_aosp_android12_phone",
  branch="aosp-android12-gsi",
  target="aosp_cf_x86_64_phone-userdebug"
)

launch_cvd_boot_test(
  name="fetch_and_launch_aosp_android11_phone",
  branch="aosp-android11-gsi",
  target="aosp_cf_x86_64_phone-userdebug"
)

launch_cvd_boot_test(
  name="fetch_and_launch_aosp_android10_phone",
  branch="aosp-android10-gsi",
  target="aosp_cf_x86_64_phone-userdebug"
)

# cvd load tests
cvd_load_boot_test(
  name="load_aosp_main_x64_phone",
  env_file="environment_specs/aosp_main_x64_phone.json",
)

cvd_load_boot_test(
  name="load_aosp_main_x64_phone_x2",
  env_file="environment_specs/aosp_main_x64_phone_x2.json",
  size="enormous",
)

# TODO(b/329100411) test loading older branches
