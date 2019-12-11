# USB/IP server library

This folder contains set of classes and structures that constitute basic USB/IP 
server.

Protocol used in this library is defined as part of
[Linux kernel documentation](https://www.kernel.org/doc/Documentation/usb/usbip_protocol.txt).

## Structure

### [`vadb::usbip::Device`](./device.h)[](#Device)

Structure describing individual device accessible over USB/IP protocol.

### [`vadb::usbip::DevicePool`](./device_pool.h)[](#DevicePool)

DevicePool holds a set of [Devices](#Device) that can be enumerated and
accessed by clients of this Server.

### [`vadb::usbip::Server`](./server.h)

Purpose of this class is to start a new listening socket and accept incoming
USB/IP connections & requests.

### [`vadb::usbip::Client`](./client.h)

Client class represents individual USB/IP connection. Client enables remote
USB/IP client to enumerate and access devices registered in
[DevicePool](#DevicePool).

### [`USB/IP Messages`](./messages.h)

This file contains structures and enum values defined by the USB/IP protocol.
All definitions found there have been collected from
[Linux kernel documentation](https://www.kernel.org/doc/Documentation/usb/usbip_protocol.txt)
.
