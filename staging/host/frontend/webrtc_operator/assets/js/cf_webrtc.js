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
  console.debug('creating data channel: ' + label);
  let dataChannel = pc.createDataChannel(label);
  // Return an object with a send function like that of the dataChannel, but
  // that only actually sends over the data channel once it has connected.
  return {
    channelPromise: new Promise((resolve, reject) => {
      dataChannel.onopen = (event) => {
        resolve(dataChannel);
      };
      dataChannel.onclose = () => {
        console.debug(
            'Data channel=' + label + ' state=' + dataChannel.readyState);
      };
      dataChannel.onmessage = onMessage ? onMessage : (msg) => {
        console.debug('Data channel=' + label + ' data="' + msg.data + '"');
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
  console.debug('expecting data channel: ' + label);
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
            console.debug(
                'Data channel=' + label + ' state=' + dataChannel.readyState);
          };
          dataChannel.onmessage = onMessage ? onMessage : (msg) => {
            console.debug('Data channel=' + label + ' data="' + msg.data + '"');
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
  constructor(pc, control, media_stream) {
    this._pc = pc;
    this._control = control;
    this._media_stream = media_stream;
    // Disable the microphone by default
    this.useMic(false);
    this._cameraDataChannel = pc.createDataChannel('camera-data-channel');
    this._cameraDataChannel.binaryType = 'arraybuffer';
    this._cameraInputQueue = new Array();
    var self = this;
    this._cameraDataChannel.onbufferedamountlow = () => {
      if (self._cameraInputQueue.length > 0) {
        self.sendCameraData(self._cameraInputQueue.shift());
      }
    };
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
    this._bluetoothChannel =
        createDataChannel(pc, 'bluetooth-channel', (msg) => {
          if (this._onBluetoothMessage) {
            this._onBluetoothMessage(msg.data);
          } else {
            console.error('Received unexpected Bluetooth message');
          }
        });
    this._streams = {};
    this._streamPromiseResolvers = {};

    pc.addEventListener('track', e => {
      console.debug('Got remote stream: ', e);
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

  get imageCapture() {
    if (this._cameraSenders && this._cameraSenders.length > 0) {
      let track = this._cameraSenders[0].track;
      return new ImageCapture(track);
    }
    return undefined;
  }

  get cameraWidth() {
    return this._x_res;
  }

  get cameraHeight() {
    return this._y_res;
  }

  get cameraEnabled() {
    return this._cameraSenders && this._cameraSenders.length > 0;
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
    if (this._media_stream) {
      this._media_stream.getAudioTracks().forEach(
          track => track.enabled = in_use);
    }
  }

  async useVideo(in_use) {
    if (in_use) {
      if (this._cameraSenders) {
        console.warning('Video is already in use');
        return;
      }
      this._cameraSenders = [];
      try {
        let videoStream = await navigator.mediaDevices.getUserMedia(
            {video: true, audio: false});
        this.sendCameraResolution(videoStream);
        videoStream.getTracks().forEach(track => {
          console.info(`Using ${track.kind} device: ${track.label}`);
          this._cameraSenders.push(this._pc.addTrack(track));
        });
      } catch (e) {
        console.error('Failed to add video stream to peer connection: ', e);
      }
    } else {
      if (!this._cameraSenders) {
        return;
      }
      for (const sender of this._cameraSenders) {
        console.info(
            `Removing ${sender.track.kind} device: ${sender.track.label}`);
        let track = sender.track;
        track.stop();
        this._pc.removeTrack(sender);
      }
      delete this._cameraSenders;
    }
    this._control.renegotiateConnection();
  }

  sendCameraResolution(stream) {
    const cameraTracks = stream.getVideoTracks();
    if (cameraTracks.length > 0) {
      const settings = cameraTracks[0].getSettings();
      this._x_res = settings.width;
      this._y_res = settings.height;
      this.sendControlMessage(JSON.stringify({
        command: 'camera_settings',
        width: settings.width,
        height: settings.height,
        frame_rate: settings.frameRate,
        facing: settings.facingMode
      }));
    }
  }

  sendOrQueueCameraData(data) {
    if (this._cameraDataChannel.bufferedAmount > 0 ||
        this._cameraInputQueue.length > 0) {
      this._cameraInputQueue.push(data);
    } else {
      this.sendCameraData(data);
    }
  }

  sendCameraData(data) {
    const MAX_SIZE = 65535;
    const END_MARKER = 'EOF';
    for (let i = 0; i < data.byteLength; i += MAX_SIZE) {
      // range is clamped to the valid index range
      this._cameraDataChannel.send(data.slice(i, i + MAX_SIZE));
    }
    this._cameraDataChannel.send(END_MARKER);
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
        'connectionstatechange', evt => cb(this._pc.connectionState));
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
     * _pc: The webrtc peer connection.
     */

    this._promiseResolvers = {};

    this._wsPromise = new Promise((resolve, reject) => {
      let ws = new WebSocket(wsUrl);
      ws.onopen = () => {
        console.debug(`Connected to ${wsUrl}`);
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
        this._on_connection_failed(
            'Unrecognized message type from server: ' + type);
        console.error(message);
    }
  }

  _onDeviceMessage(message) {
    let type = message.type;
    switch (type) {
      case 'offer':
        this._onOffer({type: 'offer', sdp: message.sdp});
        break;
      case 'answer':
        this._onAnswer({type: 'answer', sdp: message.sdp});
        break;
      case 'ice-candidate':
          this._onIceCandidate(new RTCIceCandidate({
            sdpMid: message.mid,
            sdpMLineIndex: message.mLineIndex,
            candidate: message.candidate
          }));
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
    return this._wsSendJson({message_type: 'forward', payload});
  }

  async _sendClientDescription(desc) {
    console.debug('sendClientDescription');
    return this._sendToDevice({type: 'answer', sdp: desc.sdp});
  }

  async _sendIceCandidate(candidate) {
    return this._sendToDevice({type: 'ice-candidate', candidate});
  }

  async _onOffer(desc) {
    console.debug('Remote description (offer): ', desc);
    try {
      await this._pc.setRemoteDescription(desc);
      let answer = await this._pc.createAnswer();
      console.debug('Answer: ', answer);
      await this._pc.setLocalDescription(answer);
      await this._sendClientDescription(answer);
    } catch (e) {
      console.error('Error processing remote description (offer)', e)
      throw e;
    }
  }

  async _onAnswer(answer) {
    console.debug('Remote description (answer): ', answer);
    try {
      await this._pc.setRemoteDescription(answer);
    } catch (e) {
      console.error('Error processing remote description (answer)', e)
      throw e;
    }
  }

  _onIceCandidate(iceCandidate) {
    console.debug(`Remote ICE Candidate: `, iceCandidate);
    this._pc.addIceCandidate(iceCandidate);
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

  ConnectDevice(pc) {
    this._pc = pc;
    console.debug('ConnectDevice');
    // ICE candidates will be generated when we add the offer. Adding it here
    // instead of in _onOffer because this function is called once per peer
    // connection, while _onOffer may be called more than once due to
    // renegotiations.
    this._pc.addEventListener('icecandidate', evt => {
      if (evt.candidate) this._sendIceCandidate(evt.candidate);
    });
    this._sendToDevice({type: 'request-offer'});
  }

  async renegotiateConnection() {
    console.debug('Re-negotiating connection');
    let offer = await this._pc.createOffer();
    console.debug('Local description (offer): ', offer);
    await this._pc.setLocalDescription(offer);
    this._sendToDevice({type: 'offer', sdp: offer.sdp});
  }
}

function createPeerConnection(infra_config) {
  let pc_config = {iceServers: []};
  for (const stun of infra_config.ice_servers) {
    pc_config.iceServers.push({urls: 'stun:' + stun});
  }
  let pc = new RTCPeerConnection(pc_config);

  pc.addEventListener('icecandidate', evt => {
    console.debug('Local ICE Candidate: ', evt.candidate);
  });
  pc.addEventListener('iceconnectionstatechange', evt => {
    console.debug(`ICE State Change: ${pc.iceConnectionState}`);
  });
  pc.addEventListener(
      'connectionstatechange',
      evt => console.debug(
          `WebRTC Connection State Change: ${pc.connectionState}`));
  return pc;
}

export async function Connect(deviceId, options) {
  let control = new WebRTCControl(options);
  let requestRet = await control.requestDevice(deviceId);
  let deviceInfo = requestRet.deviceInfo;
  let infraConfig = requestRet.infraConfig;
  console.debug('Device available:');
  console.debug(deviceInfo);
  let pc_config = {iceServers: []};
  if (infraConfig.ice_servers && infraConfig.ice_servers.length > 0) {
    for (const server of infraConfig.ice_servers) {
      pc_config.iceServers.push(server);
    }
  }
  let pc = createPeerConnection(infraConfig);

  let mediaStream;
  try {
    mediaStream =
        await navigator.mediaDevices.getUserMedia({video: false, audio: true});
    const tracks = mediaStream.getTracks();
    tracks.forEach(track => {
      console.info(`Using ${track.kind} device: ${track.label}`);
      pc.addTrack(track, mediaStream);
    });
  } catch (e) {
    console.error('Failed to open device: ', e);
  }

  let deviceConnection = new DeviceConnection(pc, control, mediaStream);
  deviceConnection.description = deviceInfo;

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

  control.ConnectDevice(pc);

  return connected_promise;
}
