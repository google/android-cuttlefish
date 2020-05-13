// Javascript provides atob() and btoa() for base64 encoding and decoding, but
// those don't work with binary data.
class Base64 {
  static base64Array = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'
  static encode(buffer) {
    let data = new Uint8Array(buffer);
    let size = data.length;
    let ret = '';
    let i = 0;
    for (; i < size - size%3; i += 3) {
      let x1 = data[i];
      let x2 = data[i+1];
      let x3 = data[i+2];

      let accum = (x1 * 256 + x2 ) * 256 + x3;
      ret += this.base64Array[(accum  >> 18) % 64];
      ret += this.base64Array[(accum  >> 12) % 64];
      ret += this.base64Array[(accum >> 6) % 64];
      ret += this.base64Array[accum % 64];
    }
    switch (size % 3) {
      case 1:
        ret += this.base64Array[data[i] >> 2];
        ret += this.base64Array[(data[i] % 4)*16];
        ret += '==';
        break;
      case 2:
        ret += this.base64Array[data[i] >> 2];
        ret += this.base64Array[(data[i] % 4)*16 + (data[i+1] >> 4)];
        ret += this.base64Array[(data[i] % 16) * 4];
        ret += '=';
        break;
      default:
        break;
    }
    return ret;
  }
  static decode(str) {
    if ((str.length % 4) != 0) {
      throw "Invalid base 64";
    }
    let n = str.length;
    let padding = 0;
    if (n >= 1 && str[n-1] === '=') {
      padding = 1;
      if (n >= 2 && str[n-2] == '=') {
        padding = 2;
      }
    }
    let outLen = (3 * n / 4) - padding;
    let out = new Uint8Array(outLen);

    let j = 0;
    let accum = 0;
    for (let i = 0; i < n; i++) {
      let value = this.base64Array.indexOf(str[i]);
      if (str[i] === '=') {
        if (i < n - padding) {
          throw 'Invalid base 64';
        }
        value = 0;
      } else if (value < 0) {
        throw "Invalid base 64 char: " + str[i];
      }
      accum = accum * 64 + value;
      if (((i+1)%4) == 0) {
        out[j++] = accum >> 16;
        if (j < outLen) {
          out[j++] = (accum  >> 8) % 256;
        }
        if (j < outLen) {
          out[j++] = accum % 256;
        }
        accum = 0;
      }
    }

    return out.buffer;
  }
}

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
    this._streams = {};
    this._streamPromiseResolvers = {};

    pc.addEventListener('track', e => {
      console.log('Got remote stream: ', e);
      for (const stream of e.streams) {
        this._streams[stream.id] = stream;
        if (this._streamPromiseResolvers[stream.id]) {
          for (let resolver of this._streamPromiseResolvers[stream.id]) {
            resolver();
          }
          delete this._streamPromiseResolvers[stream.id];
        }
      }
    });
  }

  set description(desc) {
    this._description = desc;
  }

  get description() {
    return this._description;
  }

  getStream(stream_id) {
    return new Promise((resolve, reject) => {
      if (this._streams[stream_id]) {
        resolve(this._streams[stream_id]);
      } else {
        if (!this._streamPromiseResolvers[stream_id]) {
          this._streamPromiseResolvers[stream_id] = [];
        }
        this._streamPromiseResolvers[stream_id].push(resolve);
      }
    });
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

  // Sends binary data directly to the in-device adb daemon (skipping the host)
  sendAdbMessage(msg) {
    // TODO(b/148086548) send over data channel instead of websocket
    this._control.sendAdbMessage(Base64.encode(msg));
  }

  // Provide a callback to receive data from the in-device adb daemon
  onAdbMessage(cb) {
    // TODO(b/148086548) send over data channel instead of websocket
    this._control.onAdbMessage(msg => cb(Base64.decode(msg)));
  }
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
      case 'adb-message':
        if (this._onAdbMessage) {
          this._onAdbMessage(message.payload);
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
    this._sendToDevice({type: 'request-offer', options: this._options, is_chrome: is_chrome ? 1 : 0});
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

  sendAdbMessage(msg) {
    this._sendToDevice({type: 'adb-message', payload: msg});
  }

  onAdbMessage(cb) {
    this._onAdbMessage = cb;
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
