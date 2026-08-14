def _file_from_label(l):
    files = l.files.to_list()
    if len(files) != 1:
        fail(msg = "Unexpected number of files in target {}: {}".format(l, len(files)))
    return files[0]

"""
Due to https://www.github.com/bazelbuild/bazel/issues/21782 , a
declare_directory target cannot overlap with any other declare_file targets.
That means the entire directory has to be created at once, pushing in the
direction of implementing a tool that creates the complete packaged directory.

Without using declare_directory and regenerating the whole directory, the
implementation risks stale files staying in the build directory, making builds
less hermetic.
"""

def _package_files_impl(ctx):
    inputs = list()

    base_dir = ctx.actions.declare_directory(ctx.attr.base_dir)

    args = ["--base_dir=" + base_dir.path]

    for (dst, src) in ctx.attr.package_file_to_src.items():
        input_file = _file_from_label(src)
        inputs.append(input_file)
        args.append("--package_file_to_src=" + dst + "=" + input_file.path)

    for (dst, target) in ctx.attr.package_file_symlink_to_package_file.items():
        args.append("--package_file_symlink_to_package_file=" + dst + "=" + target)

    ctx.actions.run(
        mnemonic = "MakeOutputDir",
        outputs = [base_dir],
        inputs = inputs,
        executable = _file_from_label(ctx.attr._packager),
        arguments = args,
    )

    return [
        DefaultInfo(files = depset([base_dir])),
    ]

package_files = rule(
    attrs = {
        "base_dir": attr.string(),
        "package_file_to_src": attr.string_keyed_label_dict(
            doc = "File paths within the generated package archive to the target providing the file.",
            allow_files = True),
        "package_file_symlink_to_package_file": attr.string_dict(
            doc = """File path within the generated package archive for a symlink pointing to another package file created by "package_file_to_src".""",
        ),
        "_packager": attr.label(
            executable = True,
            cfg = "exec",
            default = Label("//cuttlefish/package:packager"),
        ),
    },
    implementation = _package_files_impl,
)
def _package_executable_impl(ctx):
    # There is a limitation on Google's bazel-based build system internally
    # that conflicts with declare_symlink. Lacking declare_symlink, this
    # creates a shell script that `exec`s the desired file.
    #
    # See discussion on b/139398567 and cl/807235633 for more information.
    tmp_forwarder = ctx.actions.declare_file(ctx.attr.name + ".script")

    base_dir = _file_from_label(ctx.attr.package[DefaultInfo])
    if base_dir.dirname != tmp_forwarder.dirname:
        fail("package_files and package_executable must be in the same directory")

    script_base = """#!/usr/bin/env bash

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

"""
    ctx.actions.write(
        output = tmp_forwarder,
        content = script_base + 'exec "${SCRIPT_DIR}"/' + base_dir.basename + "/" + ctx.attr.executable + ' "$@"\n',
        is_executable = True,
    )

    # Because ctx.actions.write cannot declare dependencies,
    # ctx.actions.run_shell is used to artifically introduce dependencies on
    # the outputs of the package_files rule. This makes it so `bazel run` on an
    # executable target will trigger a rebuild if one of the members of the
    # package needs to be rebuilt.
    executable_forwarder = ctx.actions.declare_file(ctx.attr.name)
    ctx.actions.run_shell(
        mnemonic = "CopyForwarder",
        inputs = ctx.attr.package[DefaultInfo].files.to_list() + [tmp_forwarder],
        outputs = [executable_forwarder],
        command = " ".join(["cp", tmp_forwarder.path, executable_forwarder.path])
    )

    return [
        DefaultInfo(
            executable = executable_forwarder,
            files = depset([executable_forwarder])
        ),
    ]

package_executable = rule(
    attrs = {
        "executable": attr.string(),
        "package": attr.label(),
    },
    executable = True,
    implementation = _package_executable_impl,
)
