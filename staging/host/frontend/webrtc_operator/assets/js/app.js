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
  const deviceView = document.getElementById('device_view');
  const webrtcStatusMessage = document.getElementById('webrtc_status_message');
  const adbStatusMessage = document.getElementById('adb_status_message');

  const deviceStatusMessage = document.getElementById('device_status_message');
  let connectionAttemptDuration = 0;
  const intervalMs = 500;
  let deviceStatusEllipsisCount = 0;
  let animateDeviceStatusMessage = setInterval(function() {
    deviceStatusEllipsisCount = (deviceStatusEllipsisCount + 1) % 4;
    deviceStatusMessage.textContent = 'Connecting to device'
        + '.'.repeat(deviceStatusEllipsisCount);

    connectionAttemptDuration += intervalMs;
    if (connectionAttemptDuration > 30000) {
      deviceStatusMessage.textContent += '\r\n\r\nConnection should have occurred by now.'
          + '\r\nPlease attempt to restart the guest device.'
    } else if (connectionAttemptDuration > 15000) {
      deviceStatusMessage.textContent += '\r\n\r\nConnection is taking longer than expected...'
    }
  }, intervalMs);

  deviceScreen.addEventListener('loadeddata', (evt) => {
    clearInterval(animateDeviceStatusMessage);
    deviceStatusMessage.style.display = 'none';
    resizeDeviceView();
    deviceScreen.style.visibility = 'visible';
    // Enable the buttons after the screen is visible.
    for (const [_, button] of Object.entries(buttons)) {
      if (!button.adb) {
        button.button.disabled = false;
      }
    }
    // Start the adb connection if it is not already started.
    initializeAdb();
  });

  let videoStream;
  let display_label;
  let buttons = {};
  let mouseIsDown = false;
  let deviceConnection;
  let touchIdSlotMap = new Map();
  let touchSlots = new Array();

  let bootCompleted = false;
  let adbConnected = false;
  function showBootCompletion() {
    // Screen changed messages are not reported until after boot has completed.
    // Certain default adb buttons change screen state, so wait for boot
    // completion before enabling these buttons.
    if (adbConnected && bootCompleted) {
      adbStatusMessage.className = 'connected';
      adbStatusMessage.textContent =
          'bootup and adb connection established successfully.';
      setTimeout(function() {
        adbStatusMessage.style.visibility = 'hidden';
      }, 5000);
      for (const [_, button] of Object.entries(buttons)) {
        if (button.adb) {
          button.button.disabled = false;
        }
      }
    }
  }

  function initializeAdb() {
    init_adb(
        deviceConnection,
        function() {
          adbConnected = true;
          showBootCompletion();
        },
        function() {
          adbStatusMessage.className = 'error';
          adbStatusMessage.textContent = 'adb connection failed.';
          adbStatusMessage.style.visibility = 'visible';
          for (const [_, button] of Object.entries(buttons)) {
            if (button.adb) {
              button.button.disabled = true;
            }
          }
        });
  }

  let currentRotation = 0;
  let currentDisplayDetails;
  function onControlMessage(message) {
    let message_data = JSON.parse(message.data);
    console.log(message_data)
    let metadata = message_data.metadata;
    if (message_data.event == 'VIRTUAL_DEVICE_BOOT_STARTED') {
      // Start the adb connection after receiving the BOOT_STARTED message.
      // (This is after the adbd start message. Attempting to connect
      // immediately after adbd starts causes issues.)
      initializeAdb();
    }
    if (message_data.event == 'VIRTUAL_DEVICE_BOOT_COMPLETED') {
      bootCompleted = true;
      showBootCompletion();
    }
    if (message_data.event == 'VIRTUAL_DEVICE_SCREEN_CHANGED') {
      if (metadata.rotation != currentRotation) {
        // Animate the screen rotation.
        deviceScreen.style.transition = 'transform 1s';
      } else {
        // Don't animate screen resizes, since these appear as odd sliding
        // animations if the screen is rotated due to the translateY.
        deviceScreen.style.transition = '';
      }

      currentRotation = metadata.rotation;
      updateDeviceDisplayDetails({
        dpi: metadata.dpi,
        x_res: metadata.width,
        y_res: metadata.height
      });

      resizeDeviceView();
    }
  }

  function resizeDeviceView() {
    // Auto-scale the screen based on window size.
    // Max window width of 70%, allowing space for the control panel.
    let ww = window.innerWidth * 0.7;
    let wh = window.innerHeight;
    let vw = currentDisplayDetails.x_res;
    let vh = currentDisplayDetails.y_res;
    let scaling = vw * wh > vh * ww ? ww / vw : wh / vh;
    if (currentRotation == 0) {
      deviceScreen.style.transform = null;
      deviceScreen.style.width = vw * scaling;
      deviceScreen.style.height = vh * scaling;
    } else if (currentRotation == 1) {
      deviceScreen.style.transform =
          `rotateZ(-90deg) translateY(-${vh * scaling}px)`;
      // When rotated, w and h are swapped.
      deviceScreen.style.width = vh * scaling;
      deviceScreen.style.height = vw * scaling;
    }

    // Set the deviceView size so that the control panel positions itself next
    // to the screen correctly.
    deviceView.style.width = currentRotation == 0 ? deviceScreen.style.width :
                                                    deviceScreen.style.height;
    deviceView.style.height = currentRotation == 0 ? deviceScreen.style.height :
                                                     deviceScreen.style.width;
  }
  window.onresize = resizeDeviceView;

  function createControlPanelButton(command, title, icon_name,
      listener=onControlPanelButton,
      parent_id='control_panel_default_buttons') {
    let button = document.createElement('button');
    document.getElementById(parent_id).appendChild(button);
    button.title = title;
    button.dataset.command = command;
    button.disabled = true;
    // Capture mousedown/up/out commands instead of click to enable
    // hold detection. mouseout is used to catch if the user moves the
    // mouse outside the button while holding down.
    button.addEventListener('mousedown', listener);
    button.addEventListener('mouseup', listener);
    button.addEventListener('mouseout', listener);
    // Set the button image using Material Design icons.
    // See http://google.github.io/material-design-icons
    // and https://material.io/resources/icons
    button.classList.add('material-icons');
    button.innerHTML = icon_name;
    buttons[command] = { 'button': button }
    return buttons[command];
  }
  createControlPanelButton('power', 'Power', 'power_settings_new');
  createControlPanelButton('home', 'Home', 'home');
  createControlPanelButton('menu', 'Menu', 'menu');
  createControlPanelButton('rotate', 'Rotate', 'screen_rotation', onRotateButton);
  buttons['rotate'].adb = true;
  createControlPanelButton('volumemute', 'Volume Mute', 'volume_mute');
  createControlPanelButton('volumedown', 'Volume Down', 'volume_down');
  createControlPanelButton('volumeup', 'Volume Up', 'volume_up');

  let options = {
    wsUrl: ((location.protocol == 'http:') ? 'ws://' : 'wss://') +
      location.host + '/connect_client',
  };

  function showWebrtcError() {
    webrtcStatusMessage.style.display = 'block';
    deviceStatusMessage.style.display = 'none';
    deviceScreen.style.display = 'none';
    for (const [_, button] of Object.entries(buttons)) {
      button.button.disabled = true;
    }
  }

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
      updateDeviceHardwareDetails(deviceConnection.description.hardware);
      updateDeviceDisplayDetails(deviceConnection.description.displays[0]);
      if (deviceConnection.description.custom_control_panel_buttons.length == 0) {
        document.getElementById('custom_controls_title').style.visibility = 'hidden';
      } else {
        for (const button of deviceConnection.description.custom_control_panel_buttons) {
          if (button.shell_command) {
            // This button's command is handled by sending an ADB shell command.
            createControlPanelButton(button.command, button.title, button.icon_name,
                e => onCustomShellButton(button.shell_command, e),
                'control_panel_custom_buttons');
            buttons[button.command].adb = true;
          } else {
            // This button's command is handled by custom action server.
            createControlPanelButton(button.command, button.title, button.icon_name,
                onControlPanelButton,
                'control_panel_custom_buttons');
          }
        }
      }
      deviceConnection.onControlMessage(msg => onControlMessage(msg));
      // Start the screen as hidden. Only show when data is ready.
      deviceScreen.style.visibility = 'hidden';
      // Send an initial home button press when WebRTC connects. This is needed
      // so that the device screen receives an initial frame even if WebRTC is
      // connected long after the device boots up.
      deviceConnection.sendControlMessage(JSON.stringify({
        command: 'home',
        state: 'down',
      }));
      deviceConnection.sendControlMessage(JSON.stringify({
        command: 'home',
        state: 'up',
      }));
      // Show the error message and disable buttons when the WebRTC connection fails.
      deviceConnection.onConnectionStateChange(state => {
        if (state == 'disconnected' || state == 'failed') {
          showWebrtcError();
        }
      });
  }, rejection => {
      showWebrtcError();
  });

  let hardwareDetailsText = '';
  let displayDetailsText = '';
  function updateDeviceDetailsText() {
    document.getElementById('device_details_hardware').textContent = [
      hardwareDetailsText,
      displayDetailsText,
    ].join('\n');
  }
  function updateDeviceHardwareDetails(hardware) {
    let cpus = hardware.cpus;
    let memory_mb = hardware.memory_mb;
    hardwareDetailsText = `CPUs - ${cpus}\nDevice RAM - ${memory_mb}mb`;
    updateDeviceDetailsText();
  }
  function updateDeviceDisplayDetails(display) {
    currentDisplayDetails = display;
    let dpi = display.dpi;
    let x_res = display.x_res;
    let y_res = display.y_res;
    displayDetailsText = `Display - ${x_res}x${y_res} (${dpi}DPI)`;
    updateDeviceDetailsText();
  }

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

  function onRotateButton(e) {
    // Attempt to init adb again, in case the initial connection failed.
    // This succeeds immediately if already connected.
    initializeAdb();
    if (e.type == 'mousedown') {
      adbShell(
          '/vendor/bin/cuttlefish_rotate ' +
          (currentRotation == 0 ? 'landscape' : 'portrait'))
    }
  }
  function onCustomShellButton(shell_command, e) {
    // Attempt to init adb again, in case the initial connection failed.
    // This succeeds immediately if already connected.
    initializeAdb();
    if (e.type == 'mousedown') {
      adbShell(shell_command);
    }
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

    sendEventUpdate(true, e);
  }

  function onEndDrag(e) {
    e.preventDefault();

    // console.log("mouseup at " + e.pageX + " / " + e.pageY);
    mouseIsDown = false;

    sendEventUpdate(false, e);
  }

  function onContinueDrag(e) {
    e.preventDefault();

    // console.log("mousemove at " + e.pageX + " / " + e.pageY + ", down=" +
    // mouseIsDown);
    if (mouseIsDown) {
      sendEventUpdate(true, e);
    }
  }

  function sendEventUpdate(down, e) {
    console.assert(deviceConnection, 'Can\'t send mouse update without device');
    var eventType = e.type.substring(0, 5);

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

    // The screen uses the 'object-fit: cover' property in order to completely
    // fill the element while maintaining the screen content's aspect ratio.
    // Therefore:
    // - If vh*ew > eh*vw, w is scaled so that content width == element width
    // - Otherwise,        h is scaled so that content height == element height
    const scaleWidth = videoHeight * elementWidth > videoWidth * elementHeight;

    // Convert to coordinates relative to the video by scaling.
    // (This matches the scaling used by 'object-fit: cover'.)
    //
    // This scaling is needed to translate from the in-browser x/y to the
    // on-device x/y.
    //   - When the device screen has not been resized, this is simple: scale
    //     the coordinates based on the ratio between the input video size and
    //     the in-browser size.
    //   - When the device screen has been resized, this scaling is still needed
    //     even though the in-browser size and device size are identical. This
    //     is due to the way WindowManager handles a resized screen, resized via
    //     `adb shell wm size`:
    //       - The ABS_X and ABS_Y max values of the screen retain their
    //         original values equal to the value set when launching the device
    //         (which equals the video size here).
    //       - The sent ABS_X and ABS_Y values need to be scaled based on the
    //         ratio between the max size (video size) and in-browser size.
    const scaling = scaleWidth ? videoWidth / elementWidth : videoHeight / elementHeight;

    var xArr = [];
    var yArr = [];
    var idArr = [];
    var slotArr = [];

    console.log('e.type: ' + e.type);
    if (eventType == "mouse" || eventType == "point") {
      xArr.push(e.offsetX);
      yArr.push(e.offsetY);

      let thisId = -1;
      if (eventType == "point") {
        thisId = e.pointerId;
      }

      slotArr.push(0);
      idArr.push(thisId);
    } else if (eventType == "touch") {
      // touchstart: list of touch points that became active
      // touchmove: list of touch points that changed
      // touchend: list of touch points that were removed
      let changes = e.changedTouches;
      let rect = e.target.getBoundingClientRect();
      for (var i=0; i < changes.length; i++) {
        xArr.push(changes[i].pageX - rect.left);
        yArr.push(changes[i].pageY - rect.top);
        idArr.push(changes[i].identifier);
        if (touchIdSlotMap.has(changes[i].identifier)) {
          let slot = touchIdSlotMap.get(changes[i].identifier);

          slotArr.push(slot);
          if (e.type == 'touchstart') {
            // error
            console.error('touchstart when already have slot');
            return;
          } else if (e.type == 'touchmove') {
            idArr.push(changes[i].identifier);
          } else if (e.type == 'touchend') {
            touchSlots[slot] = false;
            touchIdSlotMap.delete(changes[i].identifier);
            idArr.push(-1);
          }
        } else {
          if (e.type == 'touchstart') {
            let slot = -1;
            for (var j=0; j < touchSlots.length; j++) {
              if (!touchSlots[j]) {
                slot = j;
                break;
              }
            }
            if (slot == -1) {
              slot = touchSlots.length;
              touchSlots.push(true);
            }
            slotArr.push(slot);
            touchSlots[slot] = true;
            touchIdSlotMap.set(changes[i].identifier, slot);
            idArr.push(changes[i].identifier);
          } else if (e.type == 'touchmove') {
            // error
            console.error('touchmove when no slot');
            return;
          } else if (e.type == 'touchend') {
            // error
            console.error('touchend when no slot');
            return;
          }
        }
      }
    }

    for (var i=0; i < xArr.length; i++) {
      xArr[i] = xArr[i] * scaling;
      yArr[i] = yArr[i] * scaling;

      // Substract the offset produced by the difference in aspect ratio, if any.
      if (scaleWidth) {
        // Width was scaled, leaving excess content height, so subtract from y.
        yArr[i] -= (elementHeight * scaling - videoHeight) / 2;
      } else {
        // Height was scaled, leaving excess content width, so subtract from x.
        xArr[i] -= (elementWidth * scaling - videoWidth) / 2;
      }

      xArr[i] = Math.trunc(xArr[i]);
      yArr[i] = Math.trunc(yArr[i]);
    }

    // NOTE: Rotation is handled automatically because the CSS rotation through
    // transforms also rotates the coordinates of events on the object.

    deviceConnection.sendMultiTouch(
    {idArr, xArr, yArr, down, slotArr, display_label});
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
  let device_to_button_map = {};
  for (const dev_id of device_ids) {
    const button_id = 'connect_' + count++;
    ul.innerHTML += ('<li class="device_entry" title="Connect to ' + dev_id
                     + '">' + dev_id + '<button id="' + button_id
                     + '" >Connect</button></li>');
    device_to_button_map[dev_id] = button_id;
  }

  for (const [dev_id, button_id] of Object.entries(device_to_button_map)) {
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
