Host process substituting for a TEE (Trusted Execution Environment). Used to
run backing implementations of Android HALs that normally delegate to a TEE
environment, specifically Gatekeeper and Keymint.

Gatekeeper and Keymint communicate with `secure_env` through virtio-console
channels connected to FIFO files on the host.

Before entering the kernel, the u-boot bootloader writes some information
about the device image files into the keymint channel to prepare it with
the authenticated version number of the operating system.

[![linkage](./doc/linkage.png)](https://cs.android.com/android/platform/superproject/+/master:device/google/cuttlefish/host/commands/secure_env/doc/linkage.svg)
