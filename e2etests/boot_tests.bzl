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
