This is the prototype ivshmem-server implementation.

We are breaking from the general philosophy of ivshmem-server  inter-vm
communication.

In this prototype there is no concept of inter-vm communication. The server
itself is meant to run on the L1 Guest (or L0 even). We will call this domain
as 'host-side'. The following functions are envisoned (at least in the
prototype):

* Create the shm window, listen for qemu VM connection (and subsequent
  disconnection). Note that the server can only accomodate one qemu VM
  connection at a time.

* Parse a JSON file describing memory layout and other information. Use
  this information to initialize the shared memory.

* Create two UNIX Domain sockets. One to communicate with QEMU and the other
  to communicate with host clients.

* For QEMU, speak the ivshmem protocol, i.e. pass a vmid, pass the shm fd
  to the qemu VM along with event fds.

* For the client, speak the ad-hoc client protocol (subject to change)
  and pass the region information, shm fd and events fds.

TODO: Fault-tolerance.


Running:

1. Install the qemu packages

    ```
    cp /google/data/ro/users/gh/ghartman/goobuntu-backports/install_qemu.sh /tmp
    bash /tmp/install_qemu.sh
    ```

1. Set up python3

    ```
    sudo apt-get install python-virtualenv python3-dev
    virtualenv -p python3 .env
    source .env/bin/activate
    pip install -r requirements.txt
    ```

1. python src/ivserver.py -L vsoc_mem.json

This should start the QEMU VM.

For testing the ivhsmem-server to host-client
python src/test_client.py
Logs are mostly on so please don't be perturbed by it.

send a SIGTERM/SIGKILL to the python process to terminate/kill the server
It doesn't clear the UNIX domain sockets on this event. This will be fixed.
You can remove them by `rm -rf /tmp/ivshmem*`

`deactivate` to come out of the virtualenv created environment.
