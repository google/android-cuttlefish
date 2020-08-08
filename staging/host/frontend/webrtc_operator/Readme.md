This signaling server defines a very simple protocol to allow the establishing
of a WebRTC connection between clients and devices. It should only be used for
development purposes or for very simple applications with no security, privacy
or scalability requirements.

Serious applications should build their own signaling server, implementing the
protocol exactly as defined below (any modifications would likely require
modifications to the client and/or device which will then not be maintained by
the cuttlefish team).

The signaling server MUST expose two (different) websocket endpoints:

* wss://<addr>/register_device
* wss://<addr>/connect_client

Additional endpoints are allowed and are up to the specific applications.
Extending the messages below with additional fields should be done with extreme
care, prefixing the field names with an applciation specific word is strongly
recommended. The same holds true for creating new message types.

Devices connect to the *register_device* endpoint and send these types of
messages:

* {"message_type": "register", "device_id": <String>, "device_info": <Any>}

* {"message_type": "forward", "client_id": <Integer>, "payload": <Any>}

The server sends the device these types of messages:

* {"message_type": "config", "ice_servers": <Array of IceServer dictionaries>,
...}

* {"message_type": "client_msg", "client_id": <Integer>, "payload": <Any>}

* {"error": <String>}

Clients connect to the *connect_client* endpoint and send these types of
messages:

* {"message_type": "connect", "device_id": <String>}

* {"message_type": "forward", "payload": <Any>}

The server sends the clients these types of messages:

* {"message_type": "config", "ice_servers": <Array of IceServer dictionaries>,
...}

* {"message_type": "device_info", "device_info": <Any>}

* {"message_type": "device_msg", "payload": <Any>}

* {"error": <String>}

A typical application flow looks like this:

* **Device** connects to *register_device*

* **Device** sends **register** message

* **Server** sends **config** message to **Device**

* **Client** connects to *connect_client*

* **Client** sends **connect** message

* **Server** sends **config** message to **Client**

* **Server** sends **device_info** message to **Client**

* **Client** sends **forward** message

* **Server** sends **client_msg** message to **Device** (at this point the
device knows about the client and cand send **forward** messages intended for
it)

* **Device** sends **forward** message

* **Server** sends **device_msg** message to client

* ...

In an alternative flow, not supported by this implementation but allowed by the
design, the **Client** connects first and only receives a **config** message
from the **Server**, only after the **Device** has sent the **register** message
the **Server** sends the **device_info** messaage to the **Client**.
