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

## Mandatory Environment Configuration (Client ID Tracking)

Setting the `PODCVD_CLIENT_ID` environment variable is **MANDATORY** whenever executing `podcvd` subcommands. This ensures proper session tracking and prevents resource collisions.

### Execution Rule (Mandatory)
* You **MUST** check if the reference file `references/podcvd_client_id.md` exists within this skill directory.
* **If `references/podcvd_client_id.md` exists**: You **MUST** read its contents using `view_file` and strictly follow the platform-specific instructions provided inside to construct and pass `PODCVD_CLIENT_ID` before calling `podcvd`.
* **If `references/podcvd_client_id.md` does NOT exist**: Only as an exception, omit setting `PODCVD_CLIENT_ID` (leave it empty) and execute `podcvd` directly.

## Key Execution Guidelines & Considerations (Crucial)

> [!IMPORTANT]
> Always follow these essential execution policies when managing Cuttlefish instances:

* **ADB Connection Lifecycle**: Subcommands (e.g., `create`, `start`, `restart`, `powerwash`) automatically establish or re-establish ADB connections, while subcommands (e.g., `stop`, `remove`, `clear`, `reset`) automatically tear down ADB connections. Never invoke external ADB tools manually.
* **Dynamic Group Identifier Tracking**: Executing `podcvd create` initializes a new instance group and returns a JSON response containing its assigned `"group_name"`. Because `"group_name"` is the primary identifier for managing that group, you **MUST** extract and track this string (from the `create` output or `podcvd fleet`) to dynamically pass `--group_name=<group_name>` in subsequent lifecycle operations (e.g., `stop`, `start`, `restart`, `remove`).
* **Booting & Execution Duration**: Starting or booting Cuttlefish instances (e.g., executing `create`, `start`, `restart`, `powerwash`) takes approximately 1 minute for the virtual device boot process and ADB connection setup to complete. During this boot window, `podcvd fleet` will report status `"Starting"` and `adb devices -l` may temporarily show an empty list. You **MUST wait** for `podcvd fleet` status to transition to `"Running"` (up to 1-2 minutes) before verifying ADB device readiness or executing teardown operations. Do not prematurely cancel, clear, or report failure while status is still `"Starting"`.
* **Status Verification**: Always verify instance state using `podcvd fleet` **BEFORE and AFTER** state transitions. Before issuing any state-changing subcommand (e.g., `stop`, `restart`, `remove`), execute `podcvd fleet` to ensure previous operations have finished and the group is in the expected state (e.g., `"Running"`). After transitions, verify status and confirm ADB connection readiness using `podcvd fleet` and `adb devices -l`.

## Standard Ready-to-Use Commands (Recommended)

To ensure high success rates and avoid malformed options, prefer using the fully constructed standard command lines below for general tasks. Always include `PODCVD_CLIENT_ID="<client_id>"` at the start of every command line.

### 1. Create Instance Group (Establishes ADB Connection)
Creates an instance group with standard configurations and **automatically establishes ADB connections**. (For bulk creation, see the concurrency policy below).
```bash
PODCVD_CLIENT_ID="<client_id>" podcvd create --vhost_user_vsock=true --report_anonymous_usage_stats=n
```

### 2. List Active Fleets
Lists all currently running Cuttlefish instance groups.
```bash
PODCVD_CLIENT_ID="<client_id>" podcvd fleet
```

### 3. Remove Specific Group (Destroys ADB Connection)
Removes a targeted instance group and **automatically destroys corresponding ADB connections**. Replace `<group_name>` with the actual ID.
```bash
PODCVD_CLIENT_ID="<client_id>" podcvd --group_name=<group_name> remove
```

### 4. Clear All Groups (Destroys All ADB Connections)
Completely tears down all Cuttlefish instance groups and destroys all relevant ADB connections globally.
```bash
PODCVD_CLIENT_ID="<client_id>" podcvd clear
```

## Concurrency & Parallel Execution Policy (Crucial)

Unless the user **explicitly requests sequential execution**, operations targeting **different instance groups** (such as creating multiple instances or removing multiple distinct groups) **MUST ALWAYS be executed in parallel**.

User intent is paramount and overrides this rule, but parallel processing is the mandatory default for any bulk workload.

> [!WARNING]
> **Default Strict Prohibition**: When handling bulk requests (e.g., "create 10 instances" or "remove groups A, B, and C"), you **MUST NOT** execute them sequentially by default. Waiting for one task to complete before initiating the next severely degrades system performance.

**Execution Guidance for Agents:**
Launch independent `podcvd` commands concurrently as background tasks, with leveraging agent's native capability dealing with concurrent behavior if possible.

**Exceptions to Parallel Execution:**
1. **User Override**: The user explicitly asks to perform tasks sequentially (one by one).
2. **State Dependency**: Operations sequentially target the **exact same `group_name`** (e.g., creating a group and immediately modifying it).
3. **Global Operations**: The `clear` subcommand internally processes global cleanup across all groups, so manual parallelization does not apply.

## Advanced Usage

Below are the currently supported podcvd subcommands in alphabetical order:

* `bugreport`: Captures diagnostic host logs into a zip archive for troubleshooting (optionally includes device adb bugreport)
* `cache`: Manage the files cached by cvd
* `clear`: Clears the instance database, stopping any running instances first
* `create`: Create a Cuttlefish instance group
* `display`: Enables hotplug/unplug of displays from running cuttlefish virtual devices
* `env`: Enumerate + Query APIs for all gRPC services made available by this virtual device instance
* `fetch`: Retrieve build artifacts based on branch and target names
* `fleet`: Lists active devices with relevant information
* `help`: Used to display help information for other commands
* `lint`: Error checks the input virtual device json config file
* `load`: Creates and starts an instance group from a JSON configuration file
* `login`: Acquire credentials
* `logs`: List and display log files
* `monitor`: Monitor device logs (launcher, kernel, and logcat) in real-time
* `powerbtn`: Trigger power button event on the device
* `powerwash`: Reset device to first boot state
* `remove`: Remove instance groups and related artifacts from the system
* `reset`: Remove all instance groups and kills orphaned cuttlefish processes
* `restart`: Restart device
* `resume`: Resume the cuttlefish device
* `screen_recording`: Record screen contents
* `setup`: Configure the host for Cuttlefish
* `snapshot_take`: Take snapshot of the cuttlefish device
* `start`: Start all Cuttlefish Instances in a group
* `status`: Query status of a single instance group. Use `podcvd fleet` for all devices
* `stop`: Stop Cuttlefish instances
* `suspend`: Suspend the cuttlefish device
* `version`: Prints version of cvd client and cvd server

To inspect supported flags and advanced setup options for any subcommand, query built-in help directly:
```bash
PODCVD_CLIENT_ID="<client_id>" podcvd help <subcommand>
```

## Connection Details Formatting Tip (Mandatory Output Formatting)

Executing the `create` or `fleet` subcommands returns JSON outputs containing instance configuration details. When reporting these connection details in your responses, you **MUST** follow these exact structural formats:

1. **ADB Connection Address**: You **MUST** combine the host IP address and the allocated ADB port into the standard **`IP:PORT`** format (for example: **`192.168.112.1:6520`**). Never output only the port number or `adb_port: 6520`.
2. **Web UI Endpoint**: You **MUST strip away specific device paths** from the `web_access` URL and present **ONLY the base Protocol, IP, and Port** (for example, if JSON returns `"https://192.168.112.1:11443/devices/cvd_1-1-1/files/client.html"`, present it as **`https://192.168.112.1:11443`**).
