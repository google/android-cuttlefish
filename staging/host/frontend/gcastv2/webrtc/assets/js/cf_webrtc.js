class DeviceConnection {
  constructor(pc, control, dataChannel, videoStreams) {
    this._pc = pc;
    this._control = control;
    this._dataChannel = dataChannel;
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

  async sendMousePosition(x, y, down) {
    return this._control.sendMousePosition(x, y, down);
  }

  async sendMultiTouch(id, x, y, initialDown, slot) {
    return this._control.sendMultiTouch(id, x, y, initialDown, slot);
  }

  async sendKeyEvent(code, type) {
    return this._control.sendKeyEvent(code, type);
  }

  disconnect() {
    this._pc.close();
  }

  // TODO(b/148086548) adb
}


const GREETING_MSG_TYPE = 'hello';
const OFFER_MSG_TYPE = 'offer';
const ICE_CANDIDATE_MSG_TYPE = 'ice-candidate';


class WebRTCControl {
  constructor(deviceId, {
    wsProtocol = 'wss',
    wsPath = '',
    disable_audio = false,
    enable_data = false,
    bundle_tracks = false
  }) {
    /*
     * Private attributes:
     *
     * _options
     *
     * _promiseResolvers = {
     *    [GREETING_MSG_TYPE]: function to resolve the greeting response promise
     *
     *    [OFFER_MSG_TYPE]: function to resolve the offer promise
     *
     *    [ICE_CANDIDATE_MSG_TYPE]: function to resolve the ice candidate
     * promise
     * }
     *
     * _wsPromise: promises the underlying websocket, should resolve when the
     *             socket passes to OPEN state, will be rejecte/replaced by a
     *             rejected promise if an error is detected on the socket.
     *
     */

    this._options = {
      disable_audio,
      bundle_tracks,
      enable_data,
    };

    this._promiseResolvers = {};

    this._wsPromise = new Promise((resolve, reject) => {
      const wsUrl = `${wsProtocol}//${wsPath}`;
      let ws = new WebSocket(wsUrl);
      ws.onopen = () => {
        console.info('Websocket connected');
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
    const type = message.type;
    if (!(type in this._promiseResolvers)) {
      console.warn(`Unexpected message of type: ${type}`);
      return;
    }
    this._promiseResolvers[type](message);
    delete this._promiseResolvers[type];
  }

  async _wsSendJson(obj) {
    let ws = await this._wsPromise;
    return ws.send(JSON.stringify(obj));
  }

  /**
   * Send a greeting to the device, returns a promise of a greeting response.
   */
  async greet() {
    console.log('greet');
    return new Promise((resolve, reject) => {
      if (GREETING_MSG_TYPE in this._promiseResolvers) {
        const msg = 'Greeting already sent and not yet responded';
        console.error(msg);
        throw new Error(msg);
      }
      this._promiseResolvers[GREETING_MSG_TYPE] = resolve;

      this._wsSendJson({
        type: 'greeting',
        message: 'Hello, world!',
        options: this._options,
      });
    });
  }

  /**
   * Sends an offer request to the device, returns a promise of an offer.
   */
  async requestOffer() {
    console.log('requestOffer');
    return new Promise((resolve, reject) => {
      if (OFFER_MSG_TYPE in this._promiseResolvers) {
        const msg = 'Offer already requested and not yet received';
        console.error(msg);
        throw new Error(msg);
      }
      this._promiseResolvers[OFFER_MSG_TYPE] = resolve;

      const is_chrome = navigator.userAgent.indexOf('Chrome') !== -1;
      this._wsSendJson({type: 'request-offer', is_chrome: is_chrome ? 1 : 0});
    });
  }

  /**
   * Sends a remote description to the device.
   */
  async sendClientDescription(desc) {
    console.log('sendClientDescription');
    this._wsSendJson({type: 'set-client-desc', sdp: desc.sdp});
  }

  /**
   * Request an ICE candidate from the device. Returns a promise of an ice
   * candidate.
   */
  async requestICECandidate(mid) {
    console.log(`requestICECandidate(${mid})`);
    if (ICE_CANDIDATE_MSG_TYPE in this._promiseResolvers) {
      const msg = 'An ice candidate request is already pending';
      console.error(msg);
      throw new Error(msg);
    }

    let iceCandidatePromise = new Promise((resolve, reject) => {
      this._promiseResolvers[ICE_CANDIDATE_MSG_TYPE] = resolve;
    });
    this._wsSendJson({type: 'get-ice-candidate', mid: mid});

    let reply = await iceCandidatePromise;
    console.log('got reply: ', reply);

    if (reply == undefined || reply.candidate == undefined) {
      console.warn('Undefined reply or candidate');
      return null;
    }

    const replyCandidate = reply.candidate;
    const mlineIndex = reply.mlineIndex;

    const result = new RTCIceCandidate(
        {sdpMid: mid, sdpMLineIndex: mlineIndex, candidate: replyCandidate});

    console.log('got result: ', result);

    return result;
  }

  // TODO(b/150183757) these should go through data channel instead, then there
  // will be no need for the control object to be accessible from this class.
  async sendMousePosition(x, y, down) {
    return this._wsSendJson({
      type: 'set-mouse-position',
      down: down ? 1 : 0,
      x,
      y,
    });
  }
  async sendMultiTouch(id, x, y, initialDown, slot) {
    return this._wsSendJson({
      type: 'inject-multi-touch',
      id,
      x,
      y,
      initialDown: initialDown ? 1 : 0,
      slot,
    });
  }
  async sendKeyEvent(code, type) {
    return this._wsSendJson(
        {type: 'key-event', keycode: code, event_type: type});
  }
}

function createPeerConnection() {
  let pc = new RTCPeerConnection();
  console.log('got pc2=', pc);

  pc.addEventListener('icecandidate', e => {
    console.log('pc.onIceCandidate: ', e.candidate);
  });

  pc.addEventListener(
      'iceconnectionstatechange',
      e => console.log(`Ice State Change: ${pc.iceConnectionState}`));

  pc.addEventListener('connectionstatechange', e => {
    console.log('connection state = ' + pc.connectionState);
  });

  return pc;
}

function createDataChannel(pc) {
  let dataChannel = pc.createDataChannel('data-channel');
  dataChannel.onopen = (event) => {
    console.log(
        'handleDataChannelStatusChange state=' + dataChannel.readyState);
    dataChannel.send('Hello, world!');
  };
  dataChannel.onclose = () => {
    console.log(
        'handleDataChannelStatusChange state=' + dataChannel.readyState);
  };
  dataChannel.onmessage = () => {
    console.log('handleDataChannelMessage data="' + event.data + '"');
  };
  return dataChannel;
}

export async function Connect(deviceId, options) {
  let control = new WebRTCControl(deviceId, options);
  let pc = createPeerConnection();
  let dataChannel = createDataChannel(pc);

  let deviceConnection = new DeviceConnection(pc, control, dataChannel);
  try {
    let greetResponse = await control.greet();
    console.log('Greeting response: ', greetResponse);
    // TODO(jemoreira): get the description from the device
    deviceConnection.description = {};

    let desc = await control.requestOffer();
    console.log('Offer: ', desc);
    await pc.setRemoteDescription(desc);

    let answer = await pc.createAnswer();
    console.log('Answer: ', answer);
    // nest then() calls here to have easy access to the answer
    await pc.setLocalDescription(answer);

    await control.sendClientDescription(answer);

    for (let mid = 0; mid < 3; ++mid) {
      let iceCandidate = await control.requestICECandidate(mid);
      console.log(`ICE Candidate[${mid}]: `, iceCandidate);
      if (iceCandidate) await pc.addIceCandidate(iceCandidate);
    }

    console.log('WebRTC connection established');
    return deviceConnection;
  } catch (e) {
    console.error('Error establishing WebRTC connection: ', e);
    throw e;
  };
}
