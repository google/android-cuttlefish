## Getting started with IVSHMEM

### Prerequisites
 * QEMU (I tested with version 2.9.50 built from QEMU master branch).
   - My source directory is named qemu.
   - The build directory in my case is
     **qemu/bin/native/debug**
     The qemu binary is:
     **qemu/bin/native/debug/x86_64-softmmu/qemu-system-x86_64**
 * VNC client (I have used Remmina bundled with Goobuntu).
 * GCloud config setup to project <b> google.com:cloud-android-nested-vms </b>
      - This is because the QEMU images are stored in Google Cloud Storage.
      - To list the active project:

```bash
        $ gcloud config list
```



To set the active project

```bash
        $ gcloud config set project google.com:cloud-android-nested-vms
```


### Download images

Please make sure there is enough space (~35GiB) in your filesystem.

Download the two compressed qemu images from cloud storage:

```bash
    $ gsutil cp gs://guest_images/qemu_images/*
```

The SHA256 sum should be the following:


```bash
    $ sha256sum vm0.qcow2.gz
    fec089a151faae29880611f69ce83145ac71228852831530ce9c61ce1e4fd408

    $ sha256sum vm1.qcow2.gz
    54e6c5860e7474ed8e4c5fa76ddddc6c50bb9a75c65b7031daefd1c38356d5bb
```

Uncompress them

```bash
  $ gunzip *
```


(Will take a while....)

### Start the server and QEMU instances
Among other things the server is responsible for
     - Creating the shmem.
     - Communicating with the QEMU instances using the ivshmem protocol over
       a UNIX Domain socket.
     - Monitoring the creation & termination of QEMU instances started with
       ivshmem device.
        - The server creates, destroys and communicates eventfds for the
          ivshmem enabled QEMU instances.

The ivshmem-server should be in the build-output directory.  In my case qemu/bin/native/debug

**Starting the server as a foreground process**

```bash
   $ ivshmem-server -F -v
```

If you see an error like the following:

```

   Using POSIX shared memory

   ivshmem create & bind socket /tmp/ivshmem_socket

   cannot connect to /tmp/ivshmem_socket: Address already in use.
```

   then please remove the UNIX Domain socket left from a previous session:

```bash
   $ rm /tmp/ivshmem_socket
```

   and retry.


**Start the VMs**


```bash
    $ qemu-system-x86_64 -m 2048 -enable-kvm -drive if=virtio,cache=none,file=vm0.qcow2 -chardev socket,path=/tmp/ivshmem_socket,id=ivsocket -device ivshmem,chardev=ivsocket,size=4,msi=off -net user,hostfwd=tcp::10022-:22 -net nic


```


```bash
    $ qemu-system-x86_64 -m 2048 -enable-kvm -drive if=virtio,cache=none,file=vm1.qcow2 -chardev socket,path=/tmp/ivshmem_socket,id=ivsocket -device ivshmem,chardev=ivsocket,size=4,msi=off -net user,hostfwd=tcp::20022-:22 -net nic

```


   Note that the additional arguments following -net is to enable ssh tunneling
   In other words the VMs could be ssh'd/scp'd into with ports 10022 and 20022
   respectively.

**Connect to the VNC servers provided by the QEMU instances**

   When asked for password type <b>qemu</b>


   Once connected to a VM, please open a **Terminal** for building the kernel
   module and test binaries. (This needs to be done for each VM instances).

   Verify that the shared memory is visible as a PCI device by issuing the
   following command in the VM:
```bash
   $ lspci -v
```
   Look for
```
   00:04.0 RAM memory: Red Hat, Inc Inter-VM shared memory
```

**Build the Kernel Module**

```bash
   $ cd ~/ivshmem/kernel_module
   $ make
   $ sudo insmod kvm_ivshmem.ko
```
   (If prompted for password, it is **qemu**)

**Build the Test Binaries**

```bash
   $ cd ~/ivshmem/tests/Interrupts/build
   $ make
```
   At this point you should get three executable binaries **int_lat_initator**,
   **int_lat**, and **vmid**

   Get to know the vmid (on machine 0)

```bash
   $ sudo ~/ivshmem/tests/Interrupts/build/vmid

   Our VM id is 5
```

Please repeat the above two procedures on the other VM

   Get to know the vmid (on machine 1)

```bash
   $ sudo ~/ivshmem/tests/Interrupts/build/vmid

   Our VM id is 7
```


Once the builds are complete in both the VMs, its time to measure interrupt latency.

   I have chosen to interrupt ping-pong between VMs 100,000 times using
   interrupts.

   Assuming the VM ids obtained are 5, 7
   On one of the machines (say the one with vmid 5) run **int_lat_initiator**

```bash
   $ sudo ~/ivshmem/tests/Interrupts/build/int_lat_initiator 1000000 7

   Average latency measured is 169386 cycles

```

   On the other machine (i.e. in our case the one with vmid 7) run **int_lat**

```bash
   $ sudo ~/ivshmem/tests/Interrupts/build/int_lat_initiator 100000 5

   Average latency measured is 169427 cycles

```

Note that the latency numbers will only be reported once both **int_lat_initiator** and **int_lat** completes.

