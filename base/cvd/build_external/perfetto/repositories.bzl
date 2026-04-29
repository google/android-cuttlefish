load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# https://github.com/google/perfetto/issues/2787 for Perfetto getting into the
# Central Repository.
def _perfetto_extension_impl(_):
    URL = "https://github.com/google/perfetto/archive/refs/tags/v54.0.tar.gz"
    http_archive(
        name = "perfetto",
        url = URL,
        strip_prefix = "perfetto-54.0",
        patch_cmds = [
            # Hack away all of the Android specific dependencies:
            "sed -i '/load(\"@rules_android/d' bazel/rules.bzl",
            "sed -i '/load(\"@perfetto\\/\\/bazel:run_ait_with_adb.bzl/d' bazel/rules.bzl",
            "sed -i 's|        android_binary(\\*\\*kwargs)|        return|g' bazel/rules.bzl",
            "sed -i 's|        android_library(\\*\\*kwargs)|        return|g' bazel/rules.bzl",
            "sed -i 's|        android_instrumentation_test(\\*\\*kwargs)|        return|g' bazel/rules.bzl",
        ],
    )

    # perfetto_cfg is a new_local_repository using a path relative to the top-level WORKSPACE file.
    # We replicate this with an http_archive using a bigger strip_prefix.
    http_archive(
        name = "perfetto_cfg",
        url = URL,
        strip_prefix = "perfetto-54.0/bazel/standalone",
        build_file_content = "# empty BUILD to make a bazel package",
        patch_cmds = [
            "sed -i 's|@com_google_protobuf|@protobuf|g' perfetto_cfg.bzl",
        ],
    )

perfetto_extension = module_extension(
    implementation = _perfetto_extension_impl,
)
