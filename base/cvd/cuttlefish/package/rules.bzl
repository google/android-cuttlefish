def _file_from_label(l):
    files = l.files.to_list()
    if len(files) != 1:
        fail(msg = "Unexpected number of files in target {}: {}".format(l, len(files)))
    return files[0]

def _get_common_prefix(paths):
    if not paths:
        return ""
    prefix = paths[0].split("/")
    for path in paths[1:]:
        path_components = path.split("/")
        common_length = min(len(prefix), len(path_components))
        prefix = [prefix[i] for i in range(common_length) if prefix[i] == path_components[i]]
        if not prefix:
            break

    return "/".join(prefix)

def _remove_prefix(path, path_prefix):
    path_parts = path.split("/")

    for path_prefix_part in path_prefix.split("/"):
        if path_parts[0] != path_prefix_part:
            fail("{} is not prefixed by {}".format(path, path_prefix))
        path_parts.pop(0)

    return "/".join(path_parts)

# Finds the relative path needed to create a symlink between the link file at
# `link_path` to the link target at `link_target_path`.
#
# Example:
#   _get_relative_path_for_link(
#       link_target_path = "cuttlefish-common/bin/graphics_detector",
#       link_path = "cuttlefish-common/bin/aarch64-linux-gnu/gfxstream_graphics_detector",
#   ) == "../graphics_detector"
def _get_relative_path_for_link(link_target_path, link_path):
    common_path = _get_common_prefix([link_target_path, link_path])

    relative_link_path = _remove_prefix(link_path, common_path)
    relative_link_path_depth = len(relative_link_path.split("/")) - 1

    relative_link_target_path = _remove_prefix(link_target_path, common_path)

    return "".join(["../" for i in range(relative_link_path_depth)]) + relative_link_target_path


def _package_files_impl(ctx):
    default_outputs = list()

    path_to_declared_file = dict()

    for (dst, src) in ctx.attr.package_file_to_src.items():
        out_file = ctx.actions.declare_file(dst)

        path_to_declared_file[dst] = out_file

        if dst == ctx.attr.executable:
            executable = out_file
        default_outputs.append(out_file)

        input_file = _file_from_label(src)
        ctx.actions.run_shell(
            outputs = [out_file],
            inputs = [input_file],
            command = "mkdir -p " + out_file.dirname + " && cp " + input_file.path + " " + out_file.path,
        )

    for (dst, target) in ctx.attr.package_file_symlink_to_package_file.items():
        dst_file = ctx.actions.declare_file(dst)

        default_outputs.append(dst_file)

        src_file = path_to_declared_file.get(target)
        if src_file == None:
            fail(msg = "Package file \"{}\" in \"package_file_symlink_to_package_file\" does not exist as a key in \"package_file_to_src\"".format(target))

        relative_target = _get_relative_path_for_link(target, dst)

        # https://github.com/bazelbuild/bazel/issues/14224: `ctx.actions.symlink()` is not used
        # here because that appears to a link using the absolute path whereas links relative to
        # the package are desired here.
        ctx.actions.run_shell(
            outputs = [dst_file],
            inputs = [src_file],
            command = "mkdir -p " + dst_file.dirname + " && ln -s " + relative_target + " " + dst_file.path,
        )

    return [
        DefaultInfo(
            executable = executable,
            files = depset(default_outputs),
        ),
    ]

package_files = rule(
    attrs = {
        "executable": attr.string(),
        "package_file_to_src": attr.string_keyed_label_dict(
            doc = "File paths within the generated package archive to the target providing the file.",
            allow_files = True),
        "package_file_symlink_to_package_file": attr.string_dict(
            doc = """File path within the generated package archive for a symlink pointing to another package file created by "package_file_to_src".""",
        ),
    },
    executable = True,
    implementation = _package_files_impl,
)
