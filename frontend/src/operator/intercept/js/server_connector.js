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
  // The server connector is loaded from the client.html file, which is loaded
  // with a path like "/vX/devices/{deviceId}/files/"
  let pathElements = location.pathname.split('/');
  let devIdx = pathElements.indexOf("devices");
  if (devIdx < 0 || devIdx + 2 >= pathElements.length ||
      pathElements[devIdx + 2] != 'files') {
    // The path doesn't match our expectations
    throw 'server connector is incompatible with this server';
  }
  return pathElements[devIdx + 1];
}

// Creates a connector capable of communicating with the signaling server.
export async function createConnector() {
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

  // Provides a hint to this controller that it should expect messages from the
  // signaling server soon. This is useful for a connector which polls for
  // example which might want to poll more quickly for a period of time.
  expectMessagesSoon(durationMilliseconds) {
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

const SHORT_POLL_DELAY = 1000;

// Implementation of the Connector interface using HTTP polling
class PollingConnector extends Connector {
  #configUrl = httpUrl('infra_config');
  #connectUrl = httpUrl('polled_connections');
  #forwardUrl;
  #messagesUrl;
  #config = undefined;
  #messagesReceived = 0;
  #pollerSchedule;
  #pollQuicklyUntil = Date.now();
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
    let response = await ajaxPostJson(this.#connectUrl, {device_id: deviceId});
    let connId = response.connection_id;
    this.#forwardUrl = httpUrl(`polled_connections/${connId}/:forward`);
    this.#messagesUrl = httpUrl(`polled_connections/${connId}/messages`);

    this.#startPolling();

    return {
      deviceInfo: response.device_info,
      infraConfig: config,
    };
  }

  async sendToDevice(msg) {
    return await ajaxPostJson(this.#forwardUrl, {
      payload: msg,
    });
  }

  async #getConfig() {
    if (this.#config === undefined) {
      this.#config = await (await fetch(this.#configUrl, {
                       method: 'GET',
                       redirect: 'follow',
                     })).json();
    }
    return this.#config;
  }

  async #pollMessages() {
    let r = await fetch(
        this.#messagesUrl + `?start=${this.#messagesReceived}`, {
          method: 'GET',
          redirect: 'follow',
        })
    let arr = await r.json();
    this.#messagesReceived += arr.length;
    return arr;
  }

  #calcNextPollDelay(previousPollDelay) {
    if (Date.now() < this.#pollQuicklyUntil) {
      return SHORT_POLL_DELAY;
    } else {
      // Do exponential backoff on the polling up to 60 seconds
      return Math.min(60000, 2 * previousPollDelay);
    }
  }

  #startPolling() {
    if (this.#pollerSchedule !== undefined) {
      return;
    }

    let currentPollDelay = SHORT_POLL_DELAY;
    let pollerRoutine = async () => {
      let messages = await this.#pollMessages();

      currentPollDelay = this.#calcNextPollDelay(currentPollDelay);

      for (const message of messages) {
        this.#onDeviceMsgCb(message.payload);
        // There is at least one message, poll sooner
        currentPollDelay = SHORT_POLL_DELAY;
      }
      this.#pollerSchedule = setTimeout(pollerRoutine, currentPollDelay);
    };

    this.#pollerSchedule = setTimeout(pollerRoutine, currentPollDelay);
  }

  expectMessagesSoon(durationMilliseconds) {
    console.debug("Polling frequently for ", durationMilliseconds, " ms.");

    clearTimeout(this.#pollerSchedule);
    this.#pollerSchedule = undefined;

    this.#pollQuicklyUntil = Date.now() + durationMilliseconds;
    this.#startPolling();
  }
}

export class DisplayInfo {
  display_id = '';
  width = 0;
  height = 0;

  constructor(display_id, width, height) {
    this.display_id = display_id;
    this.width = width;
    this.height = height;
  }
}

export class DeviceDisplaysMessageImpl {
  device_id = '';
  rotation = 0;
  displays = [];
  parentController = null;

  constructor(parentController, rotation) {
    this.device_id = deviceId();
    this.parentController = parentController;
    this.rotation = rotation;
  }

  addDisplay(display_id, width, height) {
    this.displays.push(new DisplayInfo(display_id, width, height));
  }

  send() {
    this.parentController.postMessageToParent(
        DeviceFrameMessage.TYPE_DISPLAYS_INFO, this);
  }
}

export class DeviceFrameMessage {
  static TYPE_DISPLAYS_INFO = 'displays_info';

  static [Symbol.hasInstance](instance) {
    return (('type' in instance) && ('payload' in instance));
  }

  type = '';
  payload = {};

  constructor(type, payload) {
    this.type = type;
    this.payload = payload;
  }
}

class PostMsgParentController {
  createDeviceDisplaysMessage(rotation) {
    return new DeviceDisplaysMessageImpl(this, rotation);
  }

  postMessageToParent(type, payload) {
    if (window.parent === window) return;

    window.parent.postMessage(new DeviceFrameMessage(type, payload));
  }
}
