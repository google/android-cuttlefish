load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# https://github.com/google/perfetto/issues/2787 for Perfetto getting into the
# Central Repository.
def _perfetto_extension_impl(_):
    # From commit 877553985565ab10fe20ea844891aacabd0f70c7:
    URL = "https://github.com/google/perfetto/archive/refs/tags/v55.2.tar.gz"
    http_archive(
        name = "perfetto",
        url = URL,
        strip_prefix = "perfetto-55.2",
        patch_args = ["-p1"],
        patches = [
            "@//build_external/perfetto:PATCH.0001_disable_android_deps.patch",
            "@//build_external/perfetto:PATCH.0002_add_load_statements.patch",
            "@//build_external/perfetto:PATCH.0003_expose_shutdown.patch",
            "@//build_external/perfetto:PATCH.0004_expose_flush.patch",
        ],
    )
    http_archive(
        name = "perfetto_cfg",
        url = URL,
        strip_prefix = "perfetto-55.2/bazel/standalone",
        build_file_content = "# empty BUILD to make a bazel package",
        patch_cmds = [
            "sed -i 's|@com_google_protobuf|@protobuf|g' perfetto_cfg.bzl",
        ],
    )

perfetto_extension = module_extension(
    implementation = _perfetto_extension_impl,
)
