def launch_cvd_boot_test(name, branch, target):
    native.sh_test(
        name = name,
        size = "medium",
        srcs = ["launch_cvd_boot_test.sh"],
        args = ["-b", branch, "-t", target],
        tags = [
            "exclusive",
            "external",
            "no-sandbox",
        ],
    )

def cvd_load_boot_test(name, env_file, size = "medium"):
    native.sh_test(
        name = name,
        size = size,
        srcs = ["cvd_load_boot_test.sh"],
        args = [env_file],
        data = [env_file],
        tags = [
            "exclusive",
            "external",
            "no-sandbox",
        ],
    )
