/*
 * Copyright (C) 2021 The Android Open Source Project
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

'use strict';

// The public elements in this file implement the Server Connector Interface,
// part of the contract between the signaling server and the webrtc client.
// No changes that break backward compatibility are allowed here. Any new
// features must be added as a new function/class in the interface. Any
// additions to the interface must be checked for existence by the client before
// using it.

// The id of the device the client is supposed to connect to.
// The List Devices page in the signaling server may choose any way to pass the
// device id to the client page, this function retrieves that information once
// the client loaded.
// In this case the device id is passed as a parameter in the url.
export function deviceId() {
  const urlParams = new URLSearchParams(window.location.search);
  return urlParams.get('deviceId');
}

// Creates a connector capable of communicating with the signaling server.
export async function createConnector() {
  try {
    let ws = await connectWs();
    console.debug(`Connected to ${ws.url}`);
    return new WebsocketConnector(ws);
  } catch (e) {
    console.error('WebSocket error:', e);
  }
  console.warn('Failed to connect websocket, trying polling instead');

  return new PollingConnector();
}

// A connector object provides high level functions for communicating with the
// signaling server, while hiding away implementation details.
// This class is an interface and shouldn't be instantiated direclty.
// Only the public methods present in this class form part of the Server
// Connector Interface, any implementations of the interface are considered
// internal and not accessible to client code.
class Connector {
  constructor() {
    if (this.constructor == Connector) {
      throw new Error('Connector is an abstract class');
    }
  }

  // Registers a callback to receive messages from the device. A race may occur
  // if this is called after requestDevice() is called in which some device
  // messages are lost.
  onDeviceMsg(cb) {
    throw 'Not implemented!';
  }

  // Selects a particular device in the signaling server and opens the signaling
  // channel with it (but doesn't send any message to the device). Returns a
  // promise to an object with the following properties:
  // - deviceInfo: The info object provided by the device when it registered
  // with the server.
  // - infraConfig: The server's infrastructure configuration (mainly STUN and
  // TURN servers)
  // The promise may take a long time to resolve if, for example, the server
  // decides to wait for a device with the provided id to register with it. The
  // promise may be rejected if there are connectivity issues, a device with
  // that id doesn't exist or this client doesn't have rights to access that
  // device.
  async requestDevice(deviceId) {
    throw 'Not implemented!';
  }

  // Sends a message to the device selected with requestDevice. It's an error to
  // call this function before the promise from requestDevice() has resolved.
  // Returns an empty promise that is rejected when the message can not be
  // delivered, either because the device has not been requested yet or because
  // of connectivity issues.
  async sendToDevice(msg) {
    throw 'Not implemented!';
  }
}

// Returns real implementation for ParentController.
export function createParentController() {
  return new PostMsgParentController();
}

// ParentController object provides methods for sending information from device
// UI to operator UI. This class is just an interface and real implementation is
// at the operator side. This class shouldn't be instantiated directly.
class ParentController {
  constructor() {
    if (this.constructor === DeviceDisplays) {
      throw new Error('ParentController is an abstract class');
    }
  }

  // Create and return a message object that contains display information of
  // device. Created object can be sent to operator UI using send() method.
  // rotation argument is device's physycan rotation so it will be commonly
  // applied to all displays.
  createDeviceDisplaysMessage(rotation) {
    throw 'Not implemented';
  }
}

// This class represents displays information for a device. This message is
// intended to be sent to operator UI to determine panel size of device UI.
// This is an abstract class and should not be instantiated directly. This
// message is created using createDeviceDisplaysMessage method of
// ParentController. Real implementation of this class is at operator side.
export class DeviceDisplaysMessage {
  constructor(parentController, rotation) {
    if (this.constructor === DeviceDisplaysMessage) {
      throw new Error('DeviceDisplaysMessage is an abstract class');
    }
  }

  // Add a display information to deviceDisplays message.
  addDisplay(display_id, width, height) {
    throw 'Not implemented'
  }

  // Send DeviceDisplaysMessage created using createDeviceDisplaysMessage to
  // operator UI. If operator UI does not exist (in the case device web page
  // is opened directly), the message will just be ignored.
  send() {
    throw 'Not implemented'
  }
}

// End of Server Connector Interface.

// The following code is internal and shouldn't be accessed outside this file.

function httpUrl(path) {
  return location.protocol + '//' + location.host + '/' + path;
}

function websocketUrl(path) {
  return ((location.protocol == 'http:') ? 'ws://' : 'wss://') + location.host +
      '/' + path;
}

const kPollConfigUrl = httpUrl('infra_config');
const kPollConnectUrl = httpUrl('connect');
const kPollForwardUrl = httpUrl('forward');
const kPollMessagesUrl = httpUrl('poll_messages');

async function connectWs() {
  return new Promise((resolve, reject) => {
    let url = websocketUrl('connect_client');
    let ws = new WebSocket(url);
    ws.onopen = () => {
      resolve(ws);
    };
    ws.onerror = evt => {
      reject(evt);
    };
  });
}

async function ajaxPostJson(url, data) {
  const response = await fetch(url, {
    method: 'POST',
    cache: 'no-cache',
    headers: {'Content-Type': 'application/json'},
    redirect: 'follow',
    body: JSON.stringify(data),
  });
  return response.json();
}

// Implementation of the connector interface using websockets
class WebsocketConnector extends Connector {
  #websocket;
  #futures = {};
  #onDeviceMsgCb = msg =>
      console.error('Received device message without registered listener');

  onDeviceMsg(cb) {
    this.#onDeviceMsgCb = cb;
  }

  constructor(ws) {
    super();
    ws.onmessage = e => {
      let data = JSON.parse(e.data);
      this.#onWebsocketMessage(data);
    };
    this.#websocket = ws;
  }

  async requestDevice(deviceId) {
    return new Promise((resolve, reject) => {
      this.#futures.onDeviceAvailable = (device) => resolve(device);
      this.#futures.onConnectionFailed = (error) => reject(error);
      this.#wsSendJson({
        message_type: 'connect',
        device_id: deviceId,
      });
    });
  }

  async sendToDevice(msg) {
    return this.#wsSendJson({message_type: 'forward', payload: msg});
  }

  #onWebsocketMessage(message) {
    const type = message.message_type;
    if (message.error) {
      console.error(message.error);
      this.#futures.onConnectionFailed(message.error);
      return;
    }
    switch (type) {
      case 'config':
        this.#futures.infraConfig = message;
        break;
      case 'device_info':
        if (this.#futures.onDeviceAvailable) {
          this.#futures.onDeviceAvailable({
            deviceInfo: message.device_info,
            infraConfig: this.#futures.infraConfig,
          });
          delete this.#futures.onDeviceAvailable;
        } else {
          console.error('Received unsolicited device info');
        }
        break;
      case 'device_msg':
        this.#onDeviceMsgCb(message.payload);
        break;
      default:
        console.error('Unrecognized message type from server: ', type);
        this.#futures.onConnectionFailed(
            'Unrecognized message type from server: ' + type);
        console.error(message);
    }
  }

  async #wsSendJson(obj) {
    return this.#websocket.send(JSON.stringify(obj));
  }
}

// Implementation of the Connector interface using HTTP long polling
class PollingConnector extends Connector {
  #connId = undefined;
  #config = undefined;
  #pollerSchedule;
  #onDeviceMsgCb = msg =>
      console.error('Received device message without registered listener');

  onDeviceMsg(cb) {
    this.#onDeviceMsgCb = cb;
  }

  constructor() {
    super();
  }

  async requestDevice(deviceId) {
    let config = await this.#getConfig();
    let response = await ajaxPostJson(kPollConnectUrl, {device_id: deviceId});
    this.#connId = response.connection_id;

    this.#startPolling();

    return {
      deviceInfo: response.device_info,
      infraConfig: config,
    };
  }

  async sendToDevice(msg) {
    // Forward messages act like polling messages as well
    let device_messages = await this.#forward(msg);
    for (const message of device_messages) {
      this.#onDeviceMsgCb(message);
    }
  }

  async #getConfig() {
    if (this.#config === undefined) {
      this.#config = await (await fetch(kPollConfigUrl, {
                       method: 'GET',
                       redirect: 'follow',
                     })).json();
    }
    return this.#config;
  }

  async #forward(msg) {
    return await ajaxPostJson(kPollForwardUrl, {
      connection_id: this.#connId,
      payload: msg,
    });
  }

  async #pollMessages() {
    return await ajaxPostJson(kPollMessagesUrl, {
      connection_id: this.#connId,
    });
  }

  #startPolling() {
    if (this.#pollerSchedule !== undefined) {
      return;
    }

    let currentPollDelay = 1000;
    let pollerRoutine = async () => {
      let messages = await this.#pollMessages();

      // Do exponential backoff on the polling up to 60 seconds
      currentPollDelay = Math.min(60000, 2 * currentPollDelay);
      for (const message of messages) {
        this.#onDeviceMsgCb(message);
        // There is at least one message, poll sooner
        currentPollDelay = 1000;
      }
      this.#pollerSchedule = setTimeout(pollerRoutine, currentPollDelay);
    };

    this.#pollerSchedule = setTimeout(pollerRoutine, currentPollDelay);
  }
}
