def _file_from_label(l):
    files = l.files.to_list()
    if len(files) != 1:
        fail(msg = "Unexpected number of files in target {}: {}".format(l, len(files)))
    return files[0]

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
    executable_link = ctx.actions.declare_symlink(ctx.attr.name)

    base_dir = _file_from_label(ctx.attr.package[DefaultInfo])
    if base_dir.dirname != executable_link.dirname:
        fail("package_files and package_executable must be in the same directory")

    ctx.actions.symlink(
        output = executable_link,
        target_path = base_dir.basename + "/" + ctx.attr.executable,
    )
    return [
        DefaultInfo(
            executable = executable_link,
            files = depset([executable_link])
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
