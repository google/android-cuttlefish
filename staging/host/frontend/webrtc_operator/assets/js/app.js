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

function ConnectToDevice(device_id) {
  console.log('ConnectToDevice ', device_id);
  const keyboardCaptureButton = document.getElementById('keyboardCaptureBtn');
  keyboardCaptureButton.addEventListener('click', onKeyboardCaptureClick);

  const deviceScreen = document.getElementById('deviceScreen');
  deviceScreen.addEventListener('click', onInitialClick);

  function onInitialClick(e) {
    // This stupid thing makes sure that we disable controls after the first
    // click... Why not just disable controls altogether you ask? Because then
    // audio won't play because these days user-interaction is required to enable
    // audio playback...
    console.log('onInitialClick');

    deviceScreen.controls = false;
    deviceScreen.removeEventListener('click', onInitialClick);
  }

  let videoStream;
  let display_label;
  let mouseIsDown = false;
  let deviceConnection;

  let logcatBtn = document.getElementById('showLogcatBtn');
  logcatBtn.onclick = ev => {
    init_logcat(deviceConnection);
    logcatBtn.remove();
  };

  function createControlPanelButton(command, title, icon_name) {
    let button = document.createElement('button');
    document.getElementById('control_panel').appendChild(button);
    button.title = title;
    button.dataset.command = command;
    // Capture mousedown/up/out commands instead of click to enable
    // hold detection. mouseout is used to catch if the user moves the
    // mouse outside the button while holding down.
    button.addEventListener('mousedown', onControlPanelButton);
    button.addEventListener('mouseup', onControlPanelButton);
    button.addEventListener('mouseout', onControlPanelButton);
    // Set the button image using Material Design icons.
    // See http://google.github.io/material-design-icons
    // and https://material.io/resources/icons
    button.classList.add('material-icons');
    button.innerHTML = icon_name;
  }
  createControlPanelButton('power', 'Power', 'power_settings_new');
  createControlPanelButton('home', 'Home', 'home');
  createControlPanelButton('volumemute', 'Volume Mute', 'volume_mute');
  createControlPanelButton('volumedown', 'Volume Down', 'volume_down');
  createControlPanelButton('volumeup', 'Volume Up', 'volume_up');

  let options = {
    wsUrl: ((location.protocol == 'http:') ? 'ws://' : 'wss://') +
      location.host + '/connect_client',
  };

  import('./cf_webrtc.js')
    .then(webrtcModule => webrtcModule.Connect(device_id, options))
    .then(devConn => {
      deviceConnection = devConn;
      // TODO(b/143667633): get multiple display configuration from the
      // description object
      console.log(deviceConnection.description);
      let stream_id = devConn.description.displays[0].stream_id;
      devConn.getStream(stream_id).then(stream => {
        videoStream = stream;
        display_label = stream_id;
        deviceScreen.srcObject = videoStream;
      }).catch(e => console.error('Unable to get display stream: ', e));
      startMouseTracking();  // TODO stopMouseTracking() when disconnected
      // TODO(b/163080005): Call updateDeviceDetails for any dynamic device
      // details that may change after this initial connection.
      updateDeviceDetails(deviceConnection.description);
  });

  function updateDeviceDetails(deviceInfo) {
    if (deviceInfo.hardware) {
      let cpus = deviceInfo.hardware.cpus;
      let memory_mb = deviceInfo.hardware.memory_mb;
      updateDeviceDetails.hardwareDetails =
          `CPUs - ${cpus}\nDevice RAM - ${memory_mb}mb`;
    }
    if (deviceInfo.displays) {
      let dpi = deviceInfo.displays[0].dpi;
      let x_res = deviceInfo.displays[0].x_res;
      let y_res = deviceInfo.displays[0].y_res;
      updateDeviceDetails.displayDetails =
          `Display - ${x_res}x${y_res} (${dpi}DPI)`;
    }
    document.getElementById('device_details_hardware').textContent = [
        updateDeviceDetails.hardwareDetails,
        updateDeviceDetails.displayDetails,
    ].join('\n');
  }
  updateDeviceDetails.hardwareDetails = '';
  updateDeviceDetails.displayDetails = '';

  function onKeyboardCaptureClick(e) {
    const selectedClass = 'selected';
    if (keyboardCaptureButton.classList.contains(selectedClass)) {
      stopKeyboardTracking();
      keyboardCaptureButton.classList.remove(selectedClass);
    } else {
      startKeyboardTracking();
      keyboardCaptureButton.classList.add(selectedClass);
    }
  }

  function onControlPanelButton(e) {
    if (e.type == 'mouseout' && e.which == 0) {
      // Ignore mouseout events if no mouse button is pressed.
      return;
    }
    deviceConnection.sendControlMessage(JSON.stringify({
      command: e.target.dataset.command,
      state: e.type == 'mousedown' ? "down" : "up",
    }));
  }

  function startMouseTracking() {
    if (window.PointerEvent) {
      deviceScreen.addEventListener('pointerdown', onStartDrag);
      deviceScreen.addEventListener('pointermove', onContinueDrag);
      deviceScreen.addEventListener('pointerup', onEndDrag);
    } else if (window.TouchEvent) {
      deviceScreen.addEventListener('touchstart', onStartDrag);
      deviceScreen.addEventListener('touchmove', onContinueDrag);
      deviceScreen.addEventListener('touchend', onEndDrag);
    } else if (window.MouseEvent) {
      deviceScreen.addEventListener('mousedown', onStartDrag);
      deviceScreen.addEventListener('mousemove', onContinueDrag);
      deviceScreen.addEventListener('mouseup', onEndDrag);
    }
  }

  function stopMouseTracking() {
    if (window.PointerEvent) {
      deviceScreen.removeEventListener('pointerdown', onStartDrag);
      deviceScreen.removeEventListener('pointermove', onContinueDrag);
      deviceScreen.removeEventListener('pointerup', onEndDrag);
    } else if (window.TouchEvent) {
      deviceScreen.removeEventListener('touchstart', onStartDrag);
      deviceScreen.removeEventListener('touchmove', onContinueDrag);
      deviceScreen.removeEventListener('touchend', onEndDrag);
    } else if (window.MouseEvent) {
      deviceScreen.removeEventListener('mousedown', onStartDrag);
      deviceScreen.removeEventListener('mousemove', onContinueDrag);
      deviceScreen.removeEventListener('mouseup', onEndDrag);
    }
  }

  function startKeyboardTracking() {
    document.addEventListener('keydown', onKeyEvent);
    document.addEventListener('keyup', onKeyEvent);
  }

  function stopKeyboardTracking() {
    document.removeEventListener('keydown', onKeyEvent);
    document.removeEventListener('keyup', onKeyEvent);
  }

  function onStartDrag(e) {
    e.preventDefault();

    // console.log("mousedown at " + e.pageX + " / " + e.pageY);
    mouseIsDown = true;

    sendMouseUpdate(true, e);
  }

  function onEndDrag(e) {
    e.preventDefault();

    // console.log("mouseup at " + e.pageX + " / " + e.pageY);
    mouseIsDown = false;

    sendMouseUpdate(false, e);
  }

  function onContinueDrag(e) {
    e.preventDefault();

    // console.log("mousemove at " + e.pageX + " / " + e.pageY + ", down=" +
    // mouseIsDown);
    if (mouseIsDown) {
      sendMouseUpdate(true, e);
    }
  }

  function sendMouseUpdate(down, e) {
    console.assert(deviceConnection, 'Can\'t send mouse update without device');
    var x = e.offsetX;
    var y = e.offsetY;

    // Before the first video frame arrives there is no way to know width and
    // height of the device's screen, so turn every click into a click at 0x0.
    // A click at that position is not more dangerous than anywhere else since
    // the user is clicking blind anyways.
    const videoWidth = deviceScreen.videoWidth? deviceScreen.videoWidth: 1;
    const videoHeight = deviceScreen.videoHeight? deviceScreen.videoHeight: 1;
    const elementWidth = deviceScreen.offsetWidth? deviceScreen.offsetWidth: 1;
    const elementHeight = deviceScreen.offsetHeight? deviceScreen.offsetHeight: 1;

    // vh*ew > eh*vw? then scale h instead of w
    const scaleHeight = videoHeight * elementWidth > videoWidth * elementHeight;
    var elementScaling = 0, videoScaling = 0;
    if (scaleHeight) {
      elementScaling = elementHeight;
      videoScaling = videoHeight;
    } else {
      elementScaling = elementWidth;
      videoScaling = videoWidth;
    }

    // Substract the offset produced by the difference in aspect ratio if any.
    if (scaleHeight) {
      x -= (elementWidth - elementScaling * videoWidth / videoScaling) / 2;
    } else {
      y -= (elementHeight - elementScaling * videoHeight / videoScaling) / 2;
    }

    // Convert to coordinates relative to the video
    x = videoScaling * x / elementScaling;
    y = videoScaling * y / elementScaling;

    deviceConnection.sendMousePosition(
        {x: Math.trunc(x), y: Math.trunc(y), down, display_label});
  }

  function onKeyEvent(e) {
    e.preventDefault();
    console.assert(deviceConnection, 'Can\'t send key event without device');
    deviceConnection.sendKeyEvent(e.code, e.type);
  }
}

/******************************************************************************/

function ConnectDeviceCb(dev_id) {
  console.log('Connect: ' + dev_id);
  // Hide the device selection screen
  document.getElementById('device_selector').style.display = 'none';
  // Show the device control screen
  document.getElementById('device_connection').style.visibility = 'visible';
  ConnectToDevice(dev_id);
}

function ShowNewDeviceList(device_ids) {
  let ul = document.getElementById('device_list');
  ul.innerHTML = "";
  let count = 1;
  for (const dev_id of device_ids) {
    const button_id = 'connect_' + count++;
    ul.innerHTML += ('<li class="device_entry" title="Connect to ' + dev_id
                     + '">' + dev_id + '<button id="' + button_id
                     + '" >Connect</button></li>');
    document.getElementById(button_id).addEventListener(
        'click', evt => ConnectDeviceCb(dev_id));
  }
}

function UpdateDeviceList() {
  let url = ((location.protocol == 'http:') ? 'ws:' : 'wss:') + location.host +
    '/list_devices';
  let ws = new WebSocket(url);
  ws.onopen = () => {
    ws.send("give me those device ids");
  };
  ws.onmessage = msg => {
   let device_ids = JSON.parse(msg.data);
    ShowNewDeviceList(device_ids);
  };
}

// Get any devices that are already connected
UpdateDeviceList();
// Update the list at the user's request
document.getElementById('refresh_list')
    .addEventListener('click', evt => UpdateDeviceList());
