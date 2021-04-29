/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

function createDataChannel(pc, label, onMessage) {
  console.log('creating data channel: ' + label);
  let dataChannel = pc.createDataChannel(label);
  // Return an object with a send function like that of the dataChannel, but
  // that only actually sends over the data channel once it has connected.
  return {
    channelPromise: new Promise((resolve, reject) => {
      dataChannel.onopen = (event) => {
        resolve(dataChannel);
      };
      dataChannel.onclose = () => {
        console.log(
            'Data channel=' + label + ' state=' + dataChannel.readyState);
      };
      dataChannel.onmessage = onMessage ? onMessage : (msg) => {
        console.log('Data channel=' + label + ' data="' + msg.data + '"');
      };
      dataChannel.onerror = err => {
        reject(err);
      };
    }),
    send: function(msg) {
      this.channelPromise = this.channelPromise.then(channel => {
        channel.send(msg);
        return channel;
      })
    },
  };
}

function awaitDataChannel(pc, label, onMessage) {
  console.log('expecting data channel: ' + label);
  // Return an object with a send function like that of the dataChannel, but
  // that only actually sends over the data channel once it has connected.
  return {
    channelPromise: new Promise((resolve, reject) => {
      let prev_ondatachannel = pc.ondatachannel;
      pc.ondatachannel = ev => {
        let dataChannel = ev.channel;
        if (dataChannel.label == label) {
          dataChannel.onopen = (event) => {
            resolve(dataChannel);
          };
          dataChannel.onclose = () => {
            console.log(
                'Data channel=' + label + ' state=' + dataChannel.readyState);
          };
          dataChannel.onmessage = onMessage ? onMessage : (msg) => {
            console.log('Data channel=' + label + ' data="' + msg.data + '"');
          };
          dataChannel.onerror = err => {
            reject(err);
          };
        } else if (prev_ondatachannel) {
          prev_ondatachannel(ev);
        }
      };
    }),
    send: function(msg) {
      this.channelPromise = this.channelPromise.then(channel => {
        channel.send(msg);
        return channel;
      })
    },
  };
}

class DeviceConnection {
  constructor(pc, control, audio_stream) {
    this._pc = pc;
    this._control = control;
    this._audio_stream = audio_stream;
    // Disable the microphone by default
    this.useMic(false);
    this._inputChannel = createDataChannel(pc, 'input-channel');
    this._adbChannel = createDataChannel(pc, 'adb-channel', (msg) => {
      if (this._onAdbMessage) {
        this._onAdbMessage(msg.data);
      } else {
        console.error('Received unexpected ADB message');
      }
    });
    this._controlChannel = awaitDataChannel(pc, 'device-control', (msg) => {
      if (this._onControlMessage) {
        this._onControlMessage(msg);
      } else {
        console.error('Received unexpected Control message');
      }
    });
    this._bluetoothChannel = createDataChannel(pc, 'bluetooth-channel', (msg) => {
      if (this._onBluetoothMessage) {
        this._onBluetoothMessage(msg.data);
      } else {
        console.error('Received unexpected Bluetooth message');
      }
    });
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
    this._inputChannel.send(JSON.stringify(evt));
  }

  sendMousePosition({x, y, down, display_label}) {
    this._sendJsonInput({
      type: 'mouse',
      down: down ? 1 : 0,
      x,
      y,
      display_label,
    });
  }

  // TODO (b/124121375): This should probably be an array of pointer events and
  // have different properties.
  sendMultiTouch({idArr, xArr, yArr, down, slotArr, display_label}) {
    this._sendJsonInput({
      type: 'multi-touch',
      id: idArr,
      x: xArr,
      y: yArr,
      down: down ? 1 : 0,
      slot: slotArr,
      display_label: display_label,
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
    this._adbChannel.send(msg);
  }

  // Provide a callback to receive data from the in-device adb daemon
  onAdbMessage(cb) {
    this._onAdbMessage = cb;
  }

  // Send control commands to the device
  sendControlMessage(msg) {
    this._controlChannel.send(msg);
  }

  useMic(in_use) {
    if (this._audio_stream) {
      this._audio_stream.getTracks().forEach(track => track.enabled = in_use);
    }
  }

  // Provide a callback to receive control-related comms from the device
  onControlMessage(cb) {
    this._onControlMessage = cb;
  }

  sendBluetoothMessage(msg) {
    this._bluetoothChannel.send(msg);
  }

  onBluetoothMessage(cb) {
    this._onBluetoothMessage = cb;
  }

  // Provide a callback to receive connectionstatechange states.
  onConnectionStateChange(cb) {
    this._pc.addEventListener(
      'connectionstatechange',
      evt => cb(this._pc.connectionState));
  }
}


class WebRTCControl {
  constructor({
    wsUrl = '',
  }) {
    /*
     * Private attributes:
     *
     * _wsPromise: promises the underlying websocket, should resolve when the
     *             socket passes to OPEN state, will be rejecte/replaced by a
     *             rejected promise if an error is detected on the socket.
     *
     * _onOffer
     * _onIceCandidate
     */

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
      this._on_connection_failed(message.error);
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
        this._on_connection_failed('Unrecognized message type from server: ' + type);
        console.error(message);
    }
  }

  _onDeviceMessage(message) {
    let type = message.type;
    switch (type) {
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
            candidate: message.candidate
          }));
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
      this._on_connection_failed = (error) => reject(error);
      this._wsSendJson({
        message_type: 'connect',
        device_id,
      });
    });
  }

  ConnectDevice() {
    console.log('ConnectDevice');
    this._sendToDevice({type: 'request-offer'});
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
  let pc_config = {iceServers: []};
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
  pc.addEventListener(
      'connectionstatechange',
      evt =>
          console.log(`WebRTC Connection State Change: ${pc.connectionState}`));
  return pc;
}

export async function Connect(deviceId, options) {
  let control = new WebRTCControl(options);
  let requestRet = await control.requestDevice(deviceId);
  let deviceInfo = requestRet.deviceInfo;
  let infraConfig = requestRet.infraConfig;
  console.log('Device available:');
  console.log(deviceInfo);
  let pc_config = {iceServers: []};
  if (infraConfig.ice_servers && infraConfig.ice_servers.length > 0) {
    for (const server of infraConfig.ice_servers) {
      pc_config.iceServers.push(server);
    }
  }
  let pc = createPeerConnection(infraConfig, control);

  let audioStream;
  try {
    audioStream =
        await navigator.mediaDevices.getUserMedia({video: false, audio: true});
    const audioTracks = audioStream.getAudioTracks();
    if (audioTracks.length > 0) {
      console.log(`Using Audio device: ${audioTracks[0].label}, with ${
        audioTracks.length} tracks`);
      audioTracks.forEach(track => pc.addTrack(track, audioStream));
    }
  } catch (e) {
    console.error("Failed to open audio device: ", e);
  }

  let deviceConnection = new DeviceConnection(pc, control, audioStream);
  deviceConnection.description = deviceInfo;
  async function acceptOfferAndReplyAnswer(offer) {
    try {
      await pc.setRemoteDescription(offer);
      let answer = await pc.createAnswer();
      console.log('Answer: ', answer);
      await pc.setLocalDescription(answer);
      await control.sendClientDescription(answer);
    } catch (e) {
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

  pc.addEventListener('icecandidate', evt => {
    if (evt.candidate) control.sendIceCandidate(evt.candidate);
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
