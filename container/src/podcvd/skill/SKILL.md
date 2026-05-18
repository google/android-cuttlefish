---
name: podcvd
description: Manages the lifecycle of Cuttlefish instances and their groups using podcvd, including automatic management of all ADB connections.
---

# podcvd Skill Instructions

This skill fully orchestrates the lifecycle of Cuttlefish instances and their groups within container environments, **including the automatic establishment and teardown of ADB connections.**

> [!WARNING]
> **Exclusive ADB Connection Control**: This skill internally manages all required ADB connections when creating, removing, or clearing instances. You **MUST NOT** invoke external skills, MCP servers, or secondary tools to establish ADB connections to these Cuttlefish instances, as doing so will cause system conflicts.

> [!WARNING]
> **Strict Isolation from `cvd`**: `podcvd` and standard `cvd` do not share state or information. Mixing their usage will cause inconsistent states and system errors.
> * **MUST NOT** use standard `cvd` commands if this `podcvd` skill has been used in the environment.
> * **MUST NOT** use this `podcvd` skill if standard `cvd` was previously used to manage instances.
> * **Why Instances May Seem Missing**: Because they do not share state, instances created via standard `cvd` will **not** be visible to `podcvd` (e.g., `podcvd fleet` will show no instances), and vice versa. If you cannot find an expected Cuttlefish instance, verify which tool was used to create it.

## Prerequisites

* **Package Requirement**: `podcvd` is delivered via the `cuttlefish-podcvd` Debian package.
* **Initial Setup**: To use `podcvd` successfully, the `podcvd-setup` binary (installed with the package) **MUST** be executed at least once after installation to perform the necessary initialization.
* **Required Build Artifacts**: Launching Cuttlefish instances requires appropriate Cuttlefish host tools (host binaries and libraries) and Android device images (such as system.img, boot.img, and vendor.img):
  * **Inside an Android Workspace**: You must initialize the environment (e.g., `lunch <target>`) and build the required targets (e.g., `m`) to ensure that valid Cuttlefish host tools (inside `ANDROID_HOST_OUT`) and Android device images (inside `ANDROID_PRODUCT_OUT`) are generated.
  * **Outside an Android Workspace**: If you are not in an active build environment, you must ensure that compatible, pre-built Cuttlefish host tools and Android device images are already present in the working directory or are otherwise accessible via the relevant environment paths (like `ANDROID_HOST_OUT` and `ANDROID_PRODUCT_OUT`).

## Mandatory Execution Rule (Critical)

You **MUST NOT** call the `podcvd` binary directly.

You **MUST ALWAYS** execute the wrapper script located directly inside this skill's directory at:
**`scripts/podcvd_executor.sh`**

> [!TIP]
> **Agent Guidance**: To avoid path resolution errors, verify you are pointing to the correct `scripts/podcvd_executor.sh` relative to this `SKILL.md` file when executing commands.

## Standard Ready-to-Use Commands (Recommended)

To ensure high success rates and avoid malformed options, prefer using the fully constructed standard command lines below for general tasks.

### 1. Create Instance Group (Establishes ADB Connection)
Creates an instance group with standard configurations and **automatically establishes ADB connections**. (For bulk creation, see the concurrency policy below).
```bash
./scripts/podcvd_executor.sh create --vhost_user_vsock=true --report_anonymous_usage_stats=n
```

### 2. List Active Fleets
Lists all currently running Cuttlefish instance groups.
```bash
./scripts/podcvd_executor.sh fleet
```

### 3. Remove Specific Group (Destroys ADB Connection)
Removes a targeted instance group and **automatically destroys corresponding ADB connections**. Replace `<group_name>` with the actual ID.
```bash
./scripts/podcvd_executor.sh --group_name=<group_name> remove
```

### 4. Clear All Groups (Destroys All ADB Connections)
Completely tears down all Cuttlefish instance groups and destroys all relevant ADB connections globally.
```bash
./scripts/podcvd_executor.sh clear
```

## Concurrency & Parallel Execution Policy (Crucial)

Unless the user **explicitly requests sequential execution**, operations targeting **different instance groups** (such as creating multiple instances or removing multiple distinct groups) **MUST ALWAYS be executed in parallel**.

User intent is paramount and overrides this rule, but parallel processing is the mandatory default for any bulk workload.

> [!WARNING]
> **Default Strict Prohibition**: When handling bulk requests (e.g., "create 10 instances" or "remove groups A, B, and C"), you **MUST NOT** execute them sequentially by default. Waiting for one task to complete before initiating the next severely degrades system performance.

**Execution Guidance for Agents:**
Launch independent wrapper commands concurrently as background tasks, with leveraging agent's native capability dealing with concurrent behavior if possible.

**Exceptions to Parallel Execution:**
1. **User Override**: The user explicitly asks to perform tasks sequentially (one by one).
2. **State Dependency**: Operations sequentially target the **exact same `group_name`** (e.g., creating a group and immediately modifying it).
3. **Global Operations**: The `clear` subcommand internally processes global cleanup across all groups, so manual parallelization does not apply.

## Advanced Usage

Only assemble custom command lines if the user explicitly requests non-standard configurations. Treat the wrapper script exactly like the original binary:

```bash
./scripts/podcvd_executor.sh [podcvd_flags...] <subcommand> [subcommand_flags...]
```

To understand advanced setup options and detailed command usage, you should query the tool's built-in help directly:
```bash
./scripts/podcvd_executor.sh help
# or for specific subcommands:
./scripts/podcvd_executor.sh <subcommand> --help
```

## Integrated Web UI Monitoring Tip (Highly Recommended)

Executing the `create` or `fleet` subcommands returns a JSON output containing instance configuration details. Within this JSON, you will find a **`web_access`** field.

To provide the user with the **superior integrated group dashboard** (which offers a much better management view than individual instance pages), you **MUST strip away the specific device paths** and present **ONLY the base Protocol, IP, and Port**.

### URL Extraction Example
If the JSON output returns:
```json
"web_access": "https://192.168.80.1:1443/devices/cvd_1-1-1/files/client.html"
```
You must truncate the path and provide the user with this exact link:
**`https://192.168.80.1:1443`**

## Current Limitations & Temporarily Unsupported Features (Critical)

Please be aware of the following **current** technical limitations when operating Cuttlefish instances via `podcvd`. Note that these features are actively being worked on and may be supported in future updates:

* **Temporarily Unimplemented Subcommands**: Some `cvd` subcommands listed in the help output are not yet fully implemented and will explicitly return errors if executed in the current container environment.
* **GPU Acceleration Not Yet Supported**: GPU hardware acceleration is **not yet supported**. Instances currently run with software rendering.
* **Temporarily Restricted Filesystem Flag Support**: Flag support for custom filesystem paths is currently highly restricted. While paths residing within standard `ANDROID_HOST_OUT` and `ANDROID_PRODUCT_OUT` are exceptionally handled and fully supported, specifying advanced flags to point to any other custom directory paths (for example, setting custom kernel image paths outside these pre-configured directories) will likely fail. Avoid using advanced filesystem/path-redirection flags for non-standard paths until support is fully introduced.
