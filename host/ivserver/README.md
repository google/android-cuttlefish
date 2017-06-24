## Overview

This is the native ivshmem-server implementation catering to the GCE AVD
vSoC project.

We are breaking from the general philosophy of ivshmem-server inter-vm
communication.

There is no concept of inter-vm communication. The server itself is meant to
run on the L1 Guest (or L0 even). We will call this domain as 'host-side'. The
following functions are envisoned:

* Create the shared-memory window, listen for VM connection (and subsequent
  disconnection). Note that the server can only accomodate one VM connection at
  a time. This may need to be enforced as there may be mulitple VMs (but each
  of them need a dedicated shared_memory and VM <--> server UNIX Domain
  sockets).

* Parse a JSON file describing memory layout and other information. Use
  this information to initialize the shared memory.

* Create two UNIX Domain sockets. One to communicate with QEMU and the other
  to communicate with host clients.

* For QEMU, speak the ivshmem protocol, i.e. pass a vmid, pass the shm fd
  to the qemu VM along with event fds. One for host to guest signalling and the
  other for guest to host signalling. Please see:
  QEMU_SRC/docs/specs/ivshmem-spec.txt. The only twist is that the server
  pretends to be another peer VM to comply to the ivshmem protocol.

* For the client, speak the ad-hoc client protocol:

   ivshmem_client <--> ivshmem_server handshake.

   Client -> 'GET PROTOCOL_VER'
   Server -> 'PROTOCOL_VER 0'
   Client -> INFORM REGION_NAME_LEN: 0x0000000a
   Client -> GET REGION: HW_COMPOSER
   Server -> 0xffffffff(If region name not found)
   Server -> 0xAABBC000 (region start offset)
   Server -> 0xAABBC0000 (region end offset)
   Server -> <Send cmsg with guest_to_host eventfd>
   Server -> <Send cmsg with host_to_guest eventfd>
   Server -> <Send cmsg with shmfd>

 * This also launches QEMU with the appropriate parameters

Building:
  From this directory issue the following in the command line
  `bazel build src:ivserver`

Running:
  Once the binary is built using bazel. Just run it from this directory
  as the default options expect the JSON files under conf directory.
  e.g.
  `./bazel-bin/src/ivserver`

  If you encounter either
  * Error in creating shared_memory file: File exists
  * Bind failed: Address already in use

  Please run the following commands and retry launching.
  `rm /dev/shm/ivshmem`
  `rm /tmp/ivsh*`

TODO:
 * Refactor.
 * Separate the JSON configuration into QEMU and mem-layout specific files.
 * Conform to Google coding standards.
 * Add some documentation on the default options.
 * Fault Tolerance.

