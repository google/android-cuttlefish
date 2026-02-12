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

def cvd_command_boot_test(name, branch, target, cvd_command = [], credential_source = "", tags = [], env = {}, data = []):
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
        ] + tags,
        env = env,
        data = data,
    )

def metrics_test(name, branch, target, credential_source = ""):
    args = ["-b", branch, "-t", target]
    if credential_source:
        args += ["-c", credential_source]
    native.sh_test(
        name = name,
        size = "medium",
        srcs = ["metrics_test.sh"],
        args = args,
        tags = [
            "exclusive",
            "external",
            "no-sandbox",
        ],
    )

def cvd_cts_test(
        name,
        cuttlefish_branch,
        cuttlefish_target,
        cts_branch,
        cts_target,
        cts_args,
        cuttlefish_create_args = [],
        credential_source = "",
        tags = []):
    """Runs a given set of CTS tests on a given Cuttlefish build."""
    args = [
        "--cuttlefish-create-args=\"" + " ".join(cuttlefish_create_args) + "\"",
        "--cuttlefish-fetch-branch=" + cuttlefish_branch,
        "--cuttlefish-fetch-target=" + cuttlefish_target,
        "--xml-test-result-converter-path=$(location //cvd:convert_xts_xml_to_junit_xml_bin)",
        "--xts-args=\"" + " ".join(cts_args) + "\"",
        "--xts-fetch-branch=" + cts_branch,
        "--xts-fetch-target=" + cts_target,
        "--xts-type=cts",
    ]

    if credential_source:
        args.append("--credential-source=" + credential_source)

    native.sh_test(
        name = name,
        size = "enormous",
        srcs = ["cvd_xts_test.sh"],
        args = args,
        data = [
            "//cvd:convert_xts_xml_to_junit_xml_bin",
        ],
        tags = [
            "exclusive",
            "external",
            "no-sandbox",
        ] + tags,
    )

