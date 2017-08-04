# Virtual ADB

VirtualADB serves the purpose of making Cuttlefish device available locally as a
USB device. VirtualADB uses USB/IP protocol to forward USB gadget from
Cuttlefish to localhost.

## Requirements

To compile the VirtualADB package you need to install:

```
sudo apt-get install libudev-dev
```

VirtualADB requires `vhci-hcd` kernel module to be loaded. Module is part of
kernel extra modules package:

```
sudo apt-get install linux-image-extra-`uname -r`
```

## Usage

VirtualADB uses currently `virtio channel` to communicate with usb forwarder on
`cuttlefish`. The tool instruments kernel to attach remote USB device directly.
To do that, it requires super-user privileges - primarily because it's adding a
new device to your system.

To start VirtualADB simply execute:

```
sudo vadb /path/to/usb_forwarder_socket
```

where `usb_forwarder_socket` is the socket used by usb forwarder to communicate.
