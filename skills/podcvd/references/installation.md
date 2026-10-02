# podcvd Installation & Host Setup Reference

This reference specifies the two-step procedure for installing
`cuttlefish-podcvd` and initializing the host environment.

* **If `podcvd` is not installed** (`which podcvd` fails): Perform **Step 1**
  followed by **Step 2**.
* **If `podcvd` is already installed** (only host initialization is missing):
  Skip Step 1 and perform **Step 2** only.

> [!WARNING]
> **Strict Non-Interactive `sudo` Policy**:
> * Agents cannot enter passwords at interactive `sudo` prompts. Never execute
>   plain `sudo` (without `-n`) or plain `podcvd-setup` directly, as they will
>   stop responding while waiting for password input.
> * Always attempt privileged commands with `sudo -n` first. If `sudo -n` fails
>   (e.g., `sudo: a password is required`), do not retry without `-n`; halt
>   execution and guide the user with the manual commands instead.

## Step 1: Install `cuttlefish-podcvd`

Register the Cuttlefish APT repository and install `cuttlefish-podcvd`:

```bash
sudo install -m 0755 -d /etc/apt/keyrings
sudo curl -fsSL https://us-apt.pkg.dev/doc/repo-signing-key.gpg -o /etc/apt/keyrings/android-cuttlefish-artifacts.asc
sudo chmod a+r /etc/apt/keyrings/android-cuttlefish-artifacts.asc
echo \
  "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/android-cuttlefish-artifacts.asc] \
  https://us-apt.pkg.dev/projects/android-cuttlefish-artifacts android-cuttlefish-unstable main" | \
  sudo tee /etc/apt/sources.list.d/android-cuttlefish-artifacts.list > /dev/null
sudo apt update
sudo apt install -y cuttlefish-podcvd
```

1. **Attempt Non-Interactive Installation (`sudo -n`)**: First, attempt to run
   the commands above non-interactively by replacing `sudo` with `sudo -n`. If
   this succeeds, proceed to **Step 2**.
2. **Guide the User (If `sudo -n` Fails)**: If `sudo -n` fails because a
   password is required, halt execution and guide the user to run the commands
   above manually in their terminal (followed by `podcvd-setup` in **Step 2**).

## Step 2: Initialize the Host (`podcvd-setup`)

### 1. Attempt Non-Interactive Setup (`sudo -n`)

Attempt to run `podcvd-setup` non-interactively with the target username
explicitly passed (required because `EUID=0` under `sudo` bypasses the script's
non-root user detection wrapper):

```bash
sudo -n podcvd-setup "${USER:-$(id -un)}"
```

### 2. Guide the User (If `sudo -n` Fails)

If non-interactive setup fails because `sudo` requires a password, halt
execution and guide the user to run the following command manually in their
terminal:

```bash
podcvd-setup
```

## Additional Information

For further details on `podcvd` setup and usage, refer to
[`container/src/podcvd/README.md`](https://github.com/google/android-cuttlefish/blob/main/container/src/podcvd/README.md).
