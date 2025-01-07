def launch_cvd_boot_test(name, branch, target, credential_source = ""):
    args = ["-b", branch, "-t", target]
    if credential_source:
        args += ["-c", credential_source]
    native.sh_test(
        name = name,
        size = "medium",
        srcs = ["launch_cvd_boot_test.sh"],
        args = args,
        tags = [
            "exclusive",
            "external",
            "no-sandbox",
        ],
    )

def cvd_load_boot_test(name, env_file, size = "medium", credential_source = ""):
    args = ["-e", "cvd/" + env_file]
    if credential_source:
        args += ["-c", credential_source]
    native.sh_test(
        name = name,
        size = size,
        srcs = ["cvd_load_boot_test.sh"],
        args = args,
        data = [env_file],
        tags = [
            "exclusive",
            "external",
            "no-sandbox",
        ],
    )

def cvd_command_boot_test(name, branch, target, cvd_command, credential_source = ""):
    args = ["-b", branch, "-t", target]
    if credential_source:
        args += ["-c", credential_source]
    args += cvd_command
    native.sh_test(
        name = name,
        size = "medium",
        srcs = ["cvd_command_boot_test.sh"],
        args = args,
        tags = [
            "exclusive",
            "external",
            "no-sandbox",
        ],
    )
