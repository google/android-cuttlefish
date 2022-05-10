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

  constructor({url}) {
    this.#url = url;
  }

  start() {
    // Get any devices that are already connected
    this.#UpdateDeviceList();

    // Update the list at the user's request
    document.getElementById('refresh-list')
        .addEventListener('click', evt => this.#UpdateDeviceList());

    // Show all devices
    document.getElementById('show-all').addEventListener('click', evt => {
      this.#showAll();
    });
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
    for (const devId of device_ids) {
      const showId = 'connect_' + count++;
      const launchId = 'connect_' + count++;
      let entry = this.#createDeviceEntry(devId, showId, launchId);
      ul.appendChild(entry);
      let launchBtn = document.getElementById(launchId);
      launchBtn.addEventListener('click', evt => {
        this.#openDeviceInNewTab(devId);
      });
      let showBtn = document.getElementById(showId);
      showBtn.addEventListener('click', evt => {
        let shown = this.#toggleDevice(devId);
        if (shown) {
          showBtn.textContent = "visibility";
          showBtn.title = 'Hide device';
        } else {
          showBtn.textContent = "visibility_off";
          showBtn.title = 'Show device';
        }
      });
    }
  }

  #createDeviceEntry(devId, showId, launchId) {
    let li = document.createElement('li');
    li.className = 'device-entry';
    let div = document.createElement('div');
    let span = document.createElement('span');
    span.className = 'device-label';
    span.appendChild(document.createTextNode(devId));
    span.title = devId;
    let showBtn = document.createElement('span');
    showBtn.appendChild(document.createTextNode('visibility_off'));
    showBtn.title = 'View device';
    showBtn.className = 'button material-icons';
    showBtn.id = showId;
    showBtn.title = 'Show device';
    let launchBtn = document.createElement('span');
    launchBtn.appendChild(document.createTextNode('open_in_new'));
    launchBtn.className = 'button material-icons';
    launchBtn.id = launchId;
    launchBtn.title = 'Launch in new tab';
    div.appendChild(span);
    div.appendChild(showBtn);
    div.appendChild(launchBtn);
    li.appendChild(div);
    return li;
  }

  #openDeviceInNewTab(deviceId) {
    return new Promise((resolve, reject) => {
      let client = window.open(this.#deviceConnectUrl(deviceId), deviceId);
      client.addEventListener('load', evt => {
        resolve();
      });
    });
  }

  // Returns true if the device is visible
  #toggleDevice(devId) {
    let id = `device-${devId}`;
    let viewer = document.getElementById(id);
    if (viewer) {
      viewer.remove();
      return false;
    }
    viewer = document.createElement('div');
    viewer.id = id;
    viewer.className = 'device-viewer';
    let label = document.createElement('h3');
    label.appendChild(document.createTextNode(devId));
    viewer.appendChild(label);
    let iframe = document.createElement('iframe');
    iframe.src = this.#deviceConnectUrl(devId);
    iframe.title = `Device ${devId}`
    viewer.appendChild(iframe);
    let devices = document.getElementById('devices');
    devices.appendChild(viewer);
    return true;
  }

  #showAll() {
    let buttons = document.querySelectorAll('#device-selector .button');
    for (const button of buttons) {
      if (button.textContent == 'visibility_off') {
        button.click();
      }
    }
  }

  #deviceConnectUrl(deviceId) {
    return `/devices/${deviceId}/files/client.html`;
  }
}  // DeviceListApp

window.addEventListener('load', e => {
  let listDevicesUrl = '/devices';
  let deviceListApp = new DeviceListApp({url: listDevicesUrl});
  deviceListApp.start();
});
