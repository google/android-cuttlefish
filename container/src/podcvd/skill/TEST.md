# Test Plan: podcvd

This test plan is structured into two sequential verification phases: establishing the correct activation context, followed by detailed capability testing of the execution logic.

---

## Phase 1: Trigger Testing (Activation Context)
Verify that the agent correctly identifies user intent and activates the skill only when relevant.

### 1.1 Positive Triggers (Skill Must Activate)
*   "create 3 cf instances"
*   "list active cf instances"
*   "remove cf group_1"
*   "clear all cf instances"
*   "show me running cuttlefish groups"
*   "delete cuttlefish cvd_1"

### 1.2 Negative Triggers (Skill Must Remain Inactive)
*   "build android tree"
*   "run settings app tests"
*   "flash my physical device using flashall"
*   "flash pixel 8 pro with latest eng build"
*   "reconnect acloud emulator"

---

## Phase 2: Capability Testing (Command Mapping & Logic)
Verify that the agent translates specific input prompts into the exact required underlying bash commands using the wrapper script. Stateful lifecycle integrity must be maintained (teardown actions evaluate resources seeded in prior steps).

### Scenario 1: Single Instance Creation
*   **Input Prompt**: "Create a Cuttlefish instance group."
*   **Expected Underlying Command**:
    ```bash
    ./scripts/podcvd_executor.sh create --vhost_user_vsock=true --report_anonymous_usage_stats=n
    ```
*   **Verification Details**:
    *   Must execute the wrapper script; direct `podcvd` calls are strictly prohibited.
    *   Must apply both mandatory flags.
    *   **Output Parsing Example**: The agent must extract and display the connection info clearly, stripping specific device paths from the Web UI URL:
        ```text
        group_name: "cvd_1"
        Established ADB Connection: 192.168.80.1:6520
        Integrated Web UI Dashboard: https://192.168.80.1:11443
        ```
    *   **Multi-Layered State Cross-Verification**:
        1.  **Fleet Layer**: Execute `./scripts/podcvd_executor.sh fleet`. Confirm `group_name: "cvd_1"` is actively listed.
        2.  **ADB Bridge Layer**: Execute `adb devices -l`. Confirm `192.168.80.1:6520` is present and attached as a valid device.
        3.  **Application Layer (Web UI)**: Access `https://192.168.80.1:11443` via a browser or HTTP request to ensure the integrated dashboard loads successfully.

### Scenario 2: Global Reset & State Verification (Strictly Dependent on Scenario 1)
*   **Input Prompt**: "Clear all running Cuttlefish instances."
*   **Expected Underlying Command**:
    ```bash
    ./scripts/podcvd_executor.sh clear
    ```
*   **Verification Details**:
    *   Must effectively tear down the instance previously seeded in Scenario 1.
    *   **Multi-Layered State Cross-Verification**:
        1.  **Fleet Layer**: Confirm `./scripts/podcvd_executor.sh fleet` returns an empty list.
        2.  **ADB Bridge Layer**: Execute `adb devices -l`. Confirm all Cuttlefish-managed IP:port connection states are wiped globally.
        3.  **Application Layer (Web UI)**: Confirm that all associated Web UI dashboard endpoints are unreachable.

### Scenario 3: Parallel Bulk Creation & Group Name Extraction
*   **Input Prompt**: "Create two Cuttlefish instance groups."
*   **Expected Underlying Commands** (Executed concurrently via native agent concurrency):
    ```bash
    ./scripts/podcvd_executor.sh create --vhost_user_vsock=true --report_anonymous_usage_stats=n
    ./scripts/podcvd_executor.sh create --vhost_user_vsock=true --report_anonymous_usage_stats=n
    ```
*   **Verification Details**:
    *   Commands must overlap in execution rather than blocking sequentially.
    *   **Output Parsing Example**: The agent must extract the unique **`group_name`** strings and present them clearly alongside the parsed connection info:
        ```text
        Group 1 ➔ group_name: "cvd_1", ADB: 192.168.80.1:6520, Web UI: https://192.168.80.1:11443
        Group 2 ➔ group_name: "cvd_2", ADB: 192.168.80.2:6520, Web UI: https://192.168.80.2:11443
        ```
    *   **Multi-Layered State Cross-Verification**:
        1.  **Fleet Layer**: Execute `./scripts/podcvd_executor.sh fleet`. Confirm both `cvd_1` and `cvd_2` are actively listed.
        2.  **ADB Bridge Layer**: Execute `adb devices -l`. Confirm both `192.168.80.1:6520` and `192.168.80.2:6520` are attached.
        3.  **Application Layer (Web UI)**: Confirm network reachability for both independent dashboards (`https://192.168.80.1:11443` and `https://192.168.80.2:11443`).

### Scenario 4: Stateful Dynamic Teardown (Strictly Dependent on Scenario 3)
*   **Input Prompt**: "Remove the two Cuttlefish instance groups we just created."
*   **Expected Underlying Commands** (Executed concurrently via native agent concurrency):
    ```bash
    # Example: Mapping the exact group_name outputs extracted from Scenario 3
    ./scripts/podcvd_executor.sh --group_name=cvd_1 remove
    ./scripts/podcvd_executor.sh --group_name=cvd_2 remove
    ```
*   **Verification Details**:
    *   **Dynamic State Injection**: The agent must dynamically map the output values from prior steps. For instance, if Scenario 3 returned `cvd_1` and `cvd_2`, those exact strings must be injected into the `--group_name` parameters. Hardcoded placeholders are strictly prohibited.
    *   Removals must be launched concurrently using native concurrency.
    *   **Multi-Layered State Cross-Verification**:
        1.  **Fleet Layer**: Execute `./scripts/podcvd_executor.sh fleet`. Confirm `cvd_1` and `cvd_2` have been effectively purged from the output.
        2.  **ADB Bridge Layer**: Execute `adb devices -l`. Confirm `192.168.80.1:6520` and `192.168.80.2:6520` are fully disconnected and absent from the device list.
        3.  **Application Layer (Web UI)**: Confirm that all associated Web UI dashboard endpoints are unreachable.

### Scenario 5: Secondary ADB Tool Prevention
*   **Input Prompt**: "Connect ADB to my newly created Cuttlefish instance."
*   **Expected Behavior**: The agent must refuse or bypass manual secondary ADB tool invocations, explaining that ADB connection is fully managed internally by the `podcvd_executor.sh` wrapper.
