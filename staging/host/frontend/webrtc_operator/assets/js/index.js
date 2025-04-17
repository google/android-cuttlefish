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

'use strict';

class DeviceListApp {
  #url;
  #selectDeviceCb;

  constructor({url, selectDeviceCb}) {
    this.#url = url;
    this.#selectDeviceCb = selectDeviceCb;
  }

  start() {
    // Get any devices that are already connected
    this.#UpdateDeviceList();

    // Update the list at the user's request
    document.getElementById('refresh-list')
        .addEventListener('click', evt => this.#UpdateDeviceList());
  }

  async #UpdateDeviceList() {
    try {
      const device_ids = await fetch(this.#url, {
        method: 'GET',
        cache: 'no-cache',
        redirect: 'follow',
      });
      this.#ShowNewDeviceList(await device_ids.json());
    } catch (e) {
      console.error('Error getting list of device ids: ', e);
    }
  }

  #ShowNewDeviceList(device_ids) {
    let ul = document.getElementById('device-list');
    ul.innerHTML = '';
    let count = 1;
    let device_to_button_map = {};
    for (const devId of device_ids) {
      const buttonId = 'connect_' + count++;
      let entry = this.#createDeviceEntry(devId, buttonId);
      ul.appendChild(entry);
      device_to_button_map[devId] = buttonId;
    }

    for (const [devId, buttonId] of Object.entries(device_to_button_map)) {
      let button = document.getElementById(buttonId);
      button.addEventListener('click', evt => {
        this.#selectDeviceCb(devId);
      });
    }
  }

  #createDeviceEntry(devId, buttonId) {
    let li = document.createElement('li');
    li.className = 'device_entry';
    li.title = 'Connect to ' + devId;
    let div = document.createElement('div');
    let span = document.createElement('span');
    span.appendChild(document.createTextNode(devId));
    let button = document.createElement('button');
    button.id = buttonId;
    button.appendChild(document.createTextNode('Connect'));
    div.appendChild(span);
    div.appendChild(button);
    li.appendChild(div);
    return li;
  }
}  // DeviceListApp

window.addEventListener('load', e => {
  let listDevicesUrl = '/devices';
  let selectDeviceCb = deviceId => {
    return new Promise((resolve, reject) => {
      let client = window.open(`client.html?deviceId=${deviceId}`, deviceId);
      client.addEventListener('load', evt => {
        console.debug('loaded');
        resolve();
      });
    });
  };
  let deviceListApp = new DeviceListApp({url: listDevicesUrl, selectDeviceCb});
  deviceListApp.start();
});
