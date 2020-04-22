function createInputDataChannelPromise(pc) {
  console.log("creating data channel");
  let inputChannel = pc.createDataChannel('input-channel');
  return new Promise((resolve, reject) => {
    inputChannel.onopen = (event) => {
      resolve(inputChannel);
    };
    inputChannel.onclose = () => {
      console.log(
          'handleDataChannelStatusChange state=' + dataChannel.readyState);
    };
    inputChannel.onmessage = (msg) => {
      console.log('handleDataChannelMessage data="' + msg.data + '"');
    };
    inputChannel.onerror = err => {
      reject(err);
    };
  });
}

class DeviceConnection {
  constructor(pc, control) {
    this._pc = pc;
    this._control = control;
    this._inputChannelPr = createInputDataChannelPromise(pc);
    this._videoStreams = [];

    // Apparently, the only way to obtain the track and the stream at the
    // same time is by subscribing to this event.
    pc.addEventListener('track', e => {
      console.log('Got remote stream: ', e);
      if (e.track.kind === 'video') {
        this._videoStreams.push(e.streams[0]);
      }
    });
  }

  set description(desc) {
    this._description = desc;
  }

  get description() {
    return this._description;
  }

  getVideoStream(displayNum = 0) {
    return this._videoStreams[displayNum];
  }

  _sendJsonInput(evt) {
    this._inputChannelPr = this._inputChannelPr.then(inputChannel => {
      inputChannel.send(JSON.stringify(evt));
      return inputChannel;
    });
  }

  sendMousePosition({x, y, down, display = 0}) {
    this._sendJsonInput({
      type: 'mouse',
      down: down ? 1 : 0,
      x,
      y,
    });
  }

  sendMultiTouch({id, x, y, initialDown, slot, display = 0}) {
    this._sendJsonInput({
      type: 'multi-touch',
      id,
      x,
      y,
      initialDown: initialDown ? 1 : 0,
      slot,
    });
  }

  sendKeyEvent(code, type) {
    this._sendJsonInput({type: 'keyboard', keycode: code, event_type: type});
  }

  disconnect() {
    this._pc.close();
  }

  // TODO(b/148086548) adb
}


class WebRTCControl {
  constructor({
    wsUrl = '',
    disable_audio = false,
    bundle_tracks = false,
    use_tcp = true,
  }) {
    /*
     * Private attributes:
     *
     * _options
     *
     * _wsPromise: promises the underlying websocket, should resolve when the
     *             socket passes to OPEN state, will be rejecte/replaced by a
     *             rejected promise if an error is detected on the socket.
     *
     * _onOffer
     * _onIceCandidate
     */

    this._options = {
      disable_audio,
      bundle_tracks,
      use_tcp,
    };

    this._promiseResolvers = {};

    this._wsPromise = new Promise((resolve, reject) => {
      let ws = new WebSocket(wsUrl);
      ws.onopen = () => {
        console.info(`Connected to ${wsUrl}`);
        resolve(ws);
      };
      ws.onerror = evt => {
        console.error('WebSocket error:', evt);
        reject(evt);
        // If the promise was already resolved the previous line has no effect
        this._wsPromise = Promise.reject(new Error(evt));
      };
      ws.onmessage = e => {
        console.log('onmessage ' + e.data);
        let data = JSON.parse(e.data);
        this._onWebsocketMessage(data);
      };
    });
  }

  _onWebsocketMessage(message) {
    const type = message.message_type;
    if (message.error) {
      console.error(message.error);
      return;
    }
    switch (type) {
      case 'config':
        this._infra_config = message;
        break;
      case 'device_info':
        if (this._on_device_available) {
          this._on_device_available(message.device_info);
          delete this._on_device_available;
        } else {
          console.error('Received unsolicited device info');
        }
        break;
      case 'device_msg':
        this._onDeviceMessage(message.payload);
        break;
      default:
        console.error('Unrecognized message type from server: ', type);
        console.error(message);
    }
  }

  _onDeviceMessage(message) {
    let type = message.type;
    switch(type) {
      case 'offer':
        if (this._onOffer) {
          this._onOffer({type: 'offer', sdp: message.sdp});
        } else {
          console.error('Receive offer, but nothing is wating for it');
        }
        break;
      case 'ice-candidate':
        if (this._onIceCandidate) {
          this._onIceCandidate(new RTCIceCandidate({
            sdpMid: message.mid,
            sdpMLineIndex: message.mLineIndex,
            candidate: message.candidate}));
        } else {
          console.error('Received ice candidate but nothing is waiting for it');
        }
        break;
      default:
        console.error('Unrecognized message type from device: ', type);
    }
  }

  async _wsSendJson(obj) {
    let ws = await this._wsPromise;
    return ws.send(JSON.stringify(obj));
  }
  async _sendToDevice(payload) {
    this._wsSendJson({message_type: 'forward', payload});
  }

  onOffer(cb) {
    this._onOffer = cb;
  }

  onIceCandidate(cb) {
    this._onIceCandidate = cb;
  }

  async requestDevice(device_id) {
    return new Promise((resolve, reject) => {
      this._on_device_available = (deviceInfo) => resolve({
        deviceInfo,
        infraConfig: this._infra_config,
      });
      this._wsSendJson({
        message_type: 'connect',
        device_id,
      });
    });
  }

  ConnectDevice() {
    console.log('ConnectDevice');
    const is_chrome = navigator.userAgent.indexOf('Chrome') !== -1;
    this._sendToDevice({type: 'request-offer', options: this.options,is_chrome: is_chrome ? 1 : 0});
  }

  /**
   * Sends a remote description to the device.
   */
  async sendClientDescription(desc) {
    console.log('sendClientDescription');
    this._sendToDevice({type: 'answer', sdp: desc.sdp});
  }

  /**
   * Sends an ICE candidate to the device
   */
  async sendIceCandidate(candidate) {
    this._sendToDevice({type: 'ice-candidate', candidate});
  }
}

function createPeerConnection(infra_config) {
  let pc_config = {iceServers:[]};
  for (const stun of infra_config.ice_servers) {
    pc_config.iceServers.push({urls: 'stun:' + stun});
  }
  let pc = new RTCPeerConnection(pc_config);

  pc.addEventListener('icecandidate', evt => {
    console.log('Local ICE Candidate: ', evt.candidate);
  });
  pc.addEventListener('iceconnectionstatechange', evt => {
    console.log(`ICE State Change: ${pc.iceConnectionState}`);
  });
  pc.addEventListener('connectionstatechange', evt =>
    console.log(`WebRTC Connection State Change: ${pc.connectionState}`));
  return pc;
}

export async function Connect(deviceId, options) {
  let control = new WebRTCControl(options);
  let requestRet = await control.requestDevice(deviceId);
  let deviceInfo = requestRet.deviceInfo;
  let infraConfig = requestRet.infraConfig;
  console.log("Device available:");
  console.log(deviceInfo);
  let pc_config = {
    iceServers: []
  };
  if (infraConfig.ice_servers && infraConfig.ice_servers.length > 0) {
    for (const server of infraConfig.ice_servers) {
      pc_config.iceServers.push(server);
    }
  }
  let pc = createPeerConnection(infraConfig, control);
  let deviceConnection = new DeviceConnection(pc, control);
  deviceConnection.description = deviceInfo;
  async function acceptOfferAndReplyAnswer(offer) {
    try {
      await pc.setRemoteDescription(offer);
      let answer = await pc.createAnswer();
      await pc.setLocalDescription(answer);
      await control.sendClientDescription(answer);
    } catch(e) {
      console.error('Error establishing WebRTC connection: ', e)
      throw e;
    }
  }
  control.onOffer(desc => {
      console.log('Offer: ', desc);
      acceptOfferAndReplyAnswer(desc);
  });
  control.onIceCandidate(iceCandidate => {
    console.log(`Remote ICE Candidate: `, iceCandidate);
    pc.addIceCandidate(iceCandidate);
  });

  pc.addEventListener('icecandidate',
                      evt => {
                        if (evt.candidate)
                          control.sendIceCandidate(evt.candidate);
                      });
  let connected_promise = new Promise((resolve, reject) => {
    pc.addEventListener('connectionstatechange', evt => {
      let state = pc.connectionState;
      if (state == 'connected') {
        resolve(deviceConnection);
      } else if (state == 'failed') {
        reject(evt);
      }
    });
  });
  control.ConnectDevice();

  return connected_promise;
}
