# VM Configuration libraries

This package supplies library that supports VM configuration by:

* creating, initializing and verifying images used to create a VM,
* creating configuration file that can be used with `libvirt` directly.

## Details

### `FilePartition`

`FilePartition` class offers means to create (both persistent and ephemeral),
initialize and access image files.

### `GuestConfig`

`GuestConfig` class builds XML configuration string based on supplied details
that is used to initialize `libvirt` domain.
