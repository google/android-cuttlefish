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
  const keyboardCaptureCtrl = document.getElementById('keyboard-capture-control');
  createToggleControl(keyboardCaptureCtrl, "keyboard", onKeyboardCaptureToggle);
  const micCaptureCtrl = document.getElementById('mic-capture-control');
  createToggleControl(micCaptureCtrl, "mic", onMicCaptureToggle);
  const cameraCtrl = document.getElementById('camera-control');
  createToggleControl(cameraCtrl, "videocam", onVideoCaptureToggle);

  const deviceAudio = document.getElementById('device-audio');
  const deviceDisplays = document.getElementById('device-displays');
  const statusMessage = document.getElementById('status-message');

  let connectionAttemptDuration = 0;
  const intervalMs = 500;
  let deviceStatusEllipsisCount = 0;
  let animateDeviceStatusMessage = setInterval(function() {
    connectionAttemptDuration += intervalMs;
    if (connectionAttemptDuration > 30000) {
      statusMessage.className = 'error';
      statusMessage.textContent = 'Connection should have occurred by now. ' +
          'Please attempt to restart the guest device.';
    } else {
      if (connectionAttemptDuration > 15000) {
        statusMessage.textContent = 'Connection is taking longer than expected';
      } else {
        statusMessage.textContent = 'Connecting to device';
      }
      deviceStatusEllipsisCount = (deviceStatusEllipsisCount + 1) % 4;
      statusMessage.textContent += '.'.repeat(deviceStatusEllipsisCount);
    }
  }, intervalMs);

  let buttons = {};
  let mouseIsDown = false;
  let deviceConnection;
  let touchIdSlotMap = new Map();
  let touchSlots = new Array();
  let deviceStateLidSwitchOpen = null;
  let deviceStateHingeAngleValue = null;

  function showAdbConnected() {
    // Screen changed messages are not reported until after boot has completed.
    // Certain default adb buttons change screen state, so wait for boot
    // completion before enabling these buttons.
    statusMessage.className = 'connected';
    statusMessage.textContent =
        'adb connection established successfully.';
    setTimeout(function() {
      statusMessage.style.visibility = 'hidden';
    }, 5000);
    for (const [_, button] of Object.entries(buttons)) {
      if (button.adb) {
        button.button.disabled = false;
      }
    }
  }

  function initializeAdb() {
    init_adb(
        deviceConnection,
        showAdbConnected,
        function() {
          statusMessage.className = 'error';
          statusMessage.textContent = 'adb connection failed.';
          statusMessage.style.visibility = 'visible';
          for (const [_, button] of Object.entries(buttons)) {
            if (button.adb) {
              button.button.disabled = true;
            }
          }
        });
  }

  let currentRotation = 0;
  let currentDisplayDescriptions;
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
    if (message_data.event == 'VIRTUAL_DEVICE_SCREEN_CHANGED') {
      if (metadata.rotation != currentRotation) {
        // Animate the screen rotation.
        const targetRotation = metadata.rotation == 0 ? 0 : -90;

        $(deviceDisplays).animate(
          {
            textIndent: targetRotation,
          },
          {
            duration: 1000,
            step: function(now, tween) {
              resizeDeviceDisplays();
            },
          }
        );
      }

      currentRotation = metadata.rotation;
    }
    if (message_data.event == 'VIRTUAL_DEVICE_CAPTURE_IMAGE') {
      if (deviceConnection.cameraEnabled) {
        takePhoto();
      }
    }
    if (message_data.event == 'VIRTUAL_DEVICE_DISPLAY_POWER_MODE_CHANGED') {
      updateDisplayVisibility(metadata.display, metadata.mode);
    }
  }

  function updateDisplayVisibility(displayId, powerMode) {
    const display = document.getElementById('display_' + displayId).parentElement;
    if (display == null) {
      console.error('Unknown display id: ' + displayId);
      return;
    }
    switch (powerMode) {
      case 'On':
        display.style.visibility = 'visible';
        break;
      case 'Off':
        display.style.visibility = 'hidden';
        break;
      default:
        console.error('Display ' + displayId + ' has unknown display power mode: ' + powerMode);
    }
  }

  function getTransformRotation(element) {
    if (!element.style.textIndent) {
      return 0;
    }
    // Remove 'px' and convert to float.
    return parseFloat(element.style.textIndent.slice(0, -2));
  }

  let anyDeviceDisplayLoaded = false;
  function onDeviceDisplayLoaded() {
    if (anyDeviceDisplayLoaded) {
      return;
    }
    anyDeviceDisplayLoaded = true;

    clearInterval(animateDeviceStatusMessage);
    statusMessage.textContent = 'Awaiting bootup and adb connection. Please wait...';
    resizeDeviceDisplays();

    let deviceDisplayList =
      document.getElementsByClassName("device-display");
    for (const deviceDisplay of deviceDisplayList) {
      deviceDisplay.style.visibility = 'visible';
    }

    // Enable the buttons after the screen is visible.
    for (const [_, button] of Object.entries(buttons)) {
      if (!button.adb) {
        button.button.disabled = false;
      }
    }
    // Start the adb connection if it is not already started.
    initializeAdb();
  }

  // Creates a <video> element and a <div> container element for each display.
  // The extra <div> container elements are used to maintain the width and
  // height of the device as the CSS 'transform' property used on the <video>
  // element for rotating the device only affects the visuals of the element
  // and not its layout.
  function createDeviceDisplays(devConn) {
    for (const deviceDisplayDescription of currentDisplayDescriptions) {
      let deviceDisplay = document.createElement("div");
      deviceDisplay.classList.add("device-display");
      // Start the screen as hidden. Only show when data is ready.
      deviceDisplay.style.visibility = 'hidden';

      let deviceDisplayInfo = document.createElement("div");
      deviceDisplayInfo.classList.add("device-display-info");
      deviceDisplayInfo.id = deviceDisplayDescription.stream_id + '_info';
      deviceDisplay.appendChild(deviceDisplayInfo);

      let deviceDisplayVideo = document.createElement("video");
      deviceDisplayVideo.autoplay = true;
      deviceDisplayVideo.id = deviceDisplayDescription.stream_id;
      deviceDisplayVideo.classList.add("device-display-video");
      deviceDisplayVideo.addEventListener('loadeddata', (evt) => {
        onDeviceDisplayLoaded();
      });
      deviceDisplay.appendChild(deviceDisplayVideo);

      deviceDisplays.appendChild(deviceDisplay);

      let stream_id = deviceDisplayDescription.stream_id;
      devConn.getStream(stream_id).then(stream => {
        deviceDisplayVideo.srcObject = stream;
      }).catch(e => console.error('Unable to get display stream: ', e));
    }
  }

  function takePhoto() {
    const imageCapture = deviceConnection.imageCapture;
    if (imageCapture) {
      const photoSettings = {
        imageWidth: deviceConnection.cameraWidth,
        imageHeight: deviceConnection.cameraHeight
      }
      imageCapture.takePhoto(photoSettings)
        .then(blob => blob.arrayBuffer())
        .then(buffer => deviceConnection.sendOrQueueCameraData(buffer))
        .catch(error => console.log(error));
    }
  }

  function resizeDeviceDisplays() {
    // Padding between displays.
    const deviceDisplayWidthPadding = 10;
    // Padding for the display info above each display video.
    const deviceDisplayHeightPadding = 38;

    let deviceDisplayList =
      document.getElementsByClassName("device-display");
    let deviceDisplayVideoList =
      document.getElementsByClassName("device-display-video");
    let deviceDisplayInfoList =
      document.getElementsByClassName("device-display-info");

    const rotationDegrees = getTransformRotation(deviceDisplays);
    const rotationRadians = rotationDegrees * Math.PI / 180;

    // Auto-scale the screen based on window size.
    let availableWidth = deviceDisplays.clientWidth;
    let availableHeight = deviceDisplays.clientHeight - deviceDisplayHeightPadding;

    // Reserve space for padding between the displays.
    availableWidth = availableWidth -
      (currentDisplayDescriptions.length * deviceDisplayWidthPadding);

    // Loop once over all of the displays to compute the total space needed.
    let neededWidth = 0;
    let neededHeight = 0;
    for (let i = 0; i < deviceDisplayList.length; i++) {
      let deviceDisplayDescription = currentDisplayDescriptions[i];
      let deviceDisplayVideo = deviceDisplayVideoList[i];

      const originalDisplayWidth = deviceDisplayDescription.x_res;
      const originalDisplayHeight = deviceDisplayDescription.y_res;

      const neededBoundingBoxWidth =
        Math.abs(Math.cos(rotationRadians) * originalDisplayWidth) +
        Math.abs(Math.sin(rotationRadians) * originalDisplayHeight);
      const neededBoundingBoxHeight =
        Math.abs(Math.sin(rotationRadians) * originalDisplayWidth) +
        Math.abs(Math.cos(rotationRadians) * originalDisplayHeight);

      neededWidth = neededWidth + neededBoundingBoxWidth;
      neededHeight = Math.max(neededHeight, neededBoundingBoxHeight);
    }

    const scaling = Math.min(availableWidth / neededWidth,
                             availableHeight / neededHeight);

    // Loop again over all of the displays to set the sizes and positions.
    let deviceDisplayLeftOffset = 0;
    for (let i = 0; i < deviceDisplayList.length; i++) {
      let deviceDisplay = deviceDisplayList[i];
      let deviceDisplayVideo = deviceDisplayVideoList[i];
      let deviceDisplayInfo = deviceDisplayInfoList[i];
      let deviceDisplayDescription = currentDisplayDescriptions[i];

      let rotated = currentRotation == 1 ? ' (Rotated)' : '';
      deviceDisplayInfo.textContent = `Display ${i} - ` +
          `${deviceDisplayDescription.x_res}x` +
          `${deviceDisplayDescription.y_res} ` +
          `(${deviceDisplayDescription.dpi} DPI)${rotated}`;

      const originalDisplayWidth = deviceDisplayDescription.x_res;
      const originalDisplayHeight = deviceDisplayDescription.y_res;

      const scaledDisplayWidth = originalDisplayWidth * scaling;
      const scaledDisplayHeight = originalDisplayHeight * scaling;

      const neededBoundingBoxWidth =
        Math.abs(Math.cos(rotationRadians) * originalDisplayWidth) +
        Math.abs(Math.sin(rotationRadians) * originalDisplayHeight);
      const neededBoundingBoxHeight =
        Math.abs(Math.sin(rotationRadians) * originalDisplayWidth) +
        Math.abs(Math.cos(rotationRadians) * originalDisplayHeight);

      const scaledBoundingBoxWidth = neededBoundingBoxWidth * scaling;
      const scaledBoundingBoxHeight = neededBoundingBoxHeight * scaling;

      const offsetX = (scaledBoundingBoxWidth - scaledDisplayWidth) / 2;
      const offsetY = (scaledBoundingBoxHeight - scaledDisplayHeight) / 2;

      deviceDisplayVideo.style.width = scaledDisplayWidth;
      deviceDisplayVideo.style.height = scaledDisplayHeight;
      deviceDisplayVideo.style.transform =
        `translateX(${offsetX}px) ` +
        `translateY(${offsetY}px) ` +
        `rotateZ(${rotationDegrees}deg) `;

      deviceDisplay.style.left = `${deviceDisplayLeftOffset}px`;
      deviceDisplay.style.width = scaledBoundingBoxWidth;
      deviceDisplay.style.height = scaledBoundingBoxHeight;

      deviceDisplayLeftOffset =
        deviceDisplayLeftOffset +
        deviceDisplayWidthPadding +
        scaledBoundingBoxWidth;
    }
  }
  window.onresize = resizeDeviceDisplays;

  function createControlPanelButton(command, title, icon_name,
      listener=onControlPanelButton,
      parent_id='control-panel-default-buttons') {
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

  let modalOffsets = {}
  function createModalButton(button_id, modal_id, close_id) {
    const modalButton = document.getElementById(button_id);
    const modalDiv = document.getElementById(modal_id);
    const modalHeader = modalDiv.querySelector('.modal-header');
    const modalClose = document.getElementById(close_id);

    // Position the modal to the right of the show modal button.
    modalDiv.style.top = modalButton.offsetTop;
    modalDiv.style.left = modalButton.offsetWidth + 30;

    function showHideModal(show) {
      if (show) {
        modalButton.classList.add('modal-button-opened')
        modalDiv.style.display = 'block';
      } else {
        modalButton.classList.remove('modal-button-opened')
        modalDiv.style.display = 'none';
      }
    }
    // Allow the show modal button to toggle the modal,
    modalButton.addEventListener('click',
        evt => showHideModal(modalDiv.style.display != 'block'));
    // but the close button always closes.
    modalClose.addEventListener('click',
        evt => showHideModal(false));

    // Allow the modal to be dragged by the header.
    modalOffsets[modal_id] = {
      midDrag: false,
      mouseDownOffsetX: null,
      mouseDownOffsetY: null,
    }
    modalHeader.addEventListener('mousedown',
        evt => {
            modalOffsets[modal_id].midDrag = true;
            // Store the offset of the mouse location from the
            // modal's current location.
            modalOffsets[modal_id].mouseDownOffsetX =
                parseInt(modalDiv.style.left) - evt.clientX;
            modalOffsets[modal_id].mouseDownOffsetY =
                parseInt(modalDiv.style.top) - evt.clientY;
        });
    modalHeader.addEventListener('mousemove',
        evt => {
            let offsets = modalOffsets[modal_id];
            if (offsets.midDrag) {
              // Move the modal to the mouse location plus the
              // offset calculated on the initial mouse-down.
              modalDiv.style.left =
                  evt.clientX + offsets.mouseDownOffsetX;
              modalDiv.style.top =
                  evt.clientY + offsets.mouseDownOffsetY;
            }
        });
    document.addEventListener('mouseup',
        evt => {
          modalOffsets[modal_id].midDrag = false;
        });
  }

  createModalButton(
    'device-details-button', 'device-details-modal', 'device-details-close');
  createModalButton(
    'bluetooth-console-button', 'bluetooth-console-modal', 'bluetooth-console-close');

  let options = {
    wsUrl: ((location.protocol == 'http:') ? 'ws://' : 'wss://') +
      location.host + '/connect_client',
  };

  function showWebrtcError() {
    statusMessage.className = 'error';
    statusMessage.textContent = 'No connection to the guest device. ' +
        'Please ensure the WebRTC process on the host machine is active.';
    statusMessage.style.visibility = 'visible';
    deviceDisplays.style.display = 'none';
    for (const [_, button] of Object.entries(buttons)) {
      button.button.disabled = true;
    }
  }

  import('./cf_webrtc.js')
    .then(webrtcModule => webrtcModule.Connect(device_id, options))
    .then(devConn => {
      deviceConnection = devConn;

      console.log(deviceConnection.description);

      currentDisplayDescriptions = devConn.description.displays;

      createDeviceDisplays(devConn);
      for (const audio_desc of devConn.description.audio_streams) {
        let stream_id = audio_desc.stream_id;
        devConn.getStream(stream_id).then(stream => {
          deviceAudio.srcObject = stream;
        }).catch(e => console.error('Unable to get audio stream: ', e));
      }
      startMouseTracking();  // TODO stopMouseTracking() when disconnected
      updateDeviceHardwareDetails(deviceConnection.description.hardware);
      if (deviceConnection.description.custom_control_panel_buttons.length > 0) {
        document.getElementById('control-panel-custom-buttons').style.display = 'flex';
        for (const button of deviceConnection.description.custom_control_panel_buttons) {
          if (button.shell_command) {
            // This button's command is handled by sending an ADB shell command.
            createControlPanelButton(button.command, button.title, button.icon_name,
                e => onCustomShellButton(button.shell_command, e),
                'control-panel-custom-buttons');
            buttons[button.command].adb = true;
          } else if (button.device_states) {
            // This button corresponds to variable hardware device state(s).
            createControlPanelButton(button.command, button.title, button.icon_name,
                getCustomDeviceStateButtonCb(button.device_states),
                'control-panel-custom-buttons');
            for (const device_state of button.device_states) {
              // hinge_angle is currently injected via an adb shell command that
              // triggers a guest binary.
              if ('hinge_angle_value' in device_state) {
                buttons[button.command].adb = true;
              }
            }
          } else {
            // This button's command is handled by custom action server.
            createControlPanelButton(button.command, button.title, button.icon_name,
                onControlPanelButton,
                'control-panel-custom-buttons');
          }
        }
      }
      deviceConnection.onControlMessage(msg => onControlMessage(msg));
      // Show the error message and disable buttons when the WebRTC connection fails.
      deviceConnection.onConnectionStateChange(state => {
        if (state == 'disconnected' || state == 'failed') {
          showWebrtcError();
        }
      });
      deviceConnection.onBluetoothMessage(msg => {
        bluetoothConsole.addLine(decodeRootcanalMessage(msg));
      });
  }, rejection => {
      console.error('Unable to connect: ', rejection);
      showWebrtcError();
  });

  let hardwareDetailsText = '';
  let deviceStateDetailsText = '';
  function updateDeviceDetailsText() {
    document.getElementById('device-details-hardware').textContent = [
      hardwareDetailsText,
      deviceStateDetailsText,
    ].filter(e => e /*remove empty*/).join('\n');
  }
  function updateDeviceHardwareDetails(hardware) {
    let hardwareDetailsTextLines = [];
    Object.keys(hardware).forEach(function(key) {
      let value = hardware[key];
      hardwareDetailsTextLines.push(`${key} - ${value}`);
    });

    hardwareDetailsText = hardwareDetailsTextLines.join('\n');
    updateDeviceDetailsText();
  }
  function updateDeviceStateDetails() {
    let deviceStateDetailsTextLines = [];
    if (deviceStateLidSwitchOpen != null) {
      let state = deviceStateLidSwitchOpen ? 'Opened' : 'Closed';
      deviceStateDetailsTextLines.push(`Lid Switch - ${state}`);
    }
    if (deviceStateHingeAngleValue != null) {
      deviceStateDetailsTextLines.push(`Hinge Angle - ${deviceStateHingeAngleValue}`);
    }
    deviceStateDetailsText = deviceStateDetailsTextLines.join('\n');
    updateDeviceDetailsText();
  }

  function onKeyboardCaptureToggle(enabled) {
    if (enabled) {
      startKeyboardTracking();
    } else {
      stopKeyboardTracking();
    }
  }

  function onMicCaptureToggle(enabled) {
    deviceConnection.useMic(enabled);
  }

  function onVideoCaptureToggle(enabled) {
    deviceConnection.useVideo(enabled);
  }

  function cmdConsole(consoleViewName, consoleInputName) {
    let consoleView = document.getElementById(consoleViewName);

    let addString = function(str) {
      consoleView.value += str;
      consoleView.scrollTop = consoleView.scrollHeight;
    }

    let addLine = function(line) {
      addString(line + "\r\n");
    }

    let commandCallbacks = [];

    let addCommandListener = function(f) {
      commandCallbacks.push(f);
    }

    let onCommand = function(cmd) {
      cmd = cmd.trim();

      if (cmd.length == 0) return;

      commandCallbacks.forEach(f => {
        f(cmd);
      })
    }

    addCommandListener(cmd => addLine(">> " + cmd));

    let consoleInput = document.getElementById(consoleInputName);

    consoleInput.addEventListener('keydown', e => {
      if ((e.key && e.key == 'Enter') || e.keyCode == 13) {
        let command = e.target.value;

        e.target.value = '';

        onCommand(command);
      }
    })

    return {
      consoleView: consoleView,
      consoleInput: consoleInput,
      addLine: addLine,
      addString: addString,
      addCommandListener: addCommandListener,
    };
  }

  var bluetoothConsole = cmdConsole(
    'bluetooth-console-view', 'bluetooth-console-input');

  bluetoothConsole.addCommandListener(cmd => {
    let inputArr = cmd.split(' ');
    let command = inputArr[0];
    inputArr.shift();
    let args = inputArr;
    deviceConnection.sendBluetoothMessage(createRootcanalMessage(command, args));
  })

  function onControlPanelButton(e) {
    if (e.type == 'mouseout' && e.which == 0) {
      // Ignore mouseout events if no mouse button is pressed.
      return;
    }
    deviceConnection.sendControlMessage(JSON.stringify({
      command: e.target.dataset.command,
      button_state: e.type == 'mousedown' ? "down" : "up",
    }));
  }

  function onRotateButton(e) {
    // Attempt to init adb again, in case the initial connection failed.
    // This succeeds immediately if already connected.
    initializeAdb();
    if (e.type == 'mousedown') {
      adbShell(
          '/vendor/bin/cuttlefish_sensor_injection rotate ' +
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

  function getCustomDeviceStateButtonCb(device_states) {
    let states = device_states;
    let index = 0;
    return e => {
      if (e.type == 'mousedown') {
        // Reset any overridden device state.
        adbShell('cmd device_state state reset');
        // Send a device_state message for the current state.
        let message = {
          command: 'device_state',
          ...states[index],
        };
        deviceConnection.sendControlMessage(JSON.stringify(message));
        console.log(JSON.stringify(message));
        if ('lid_switch_open' in states[index]) {
          deviceStateLidSwitchOpen = states[index].lid_switch_open;
        }
        if ('hinge_angle_value' in states[index]) {
          deviceStateHingeAngleValue = states[index].hinge_angle_value;
          // TODO(b/181157794): Use a custom Sensor HAL for hinge_angle injection
          // instead of this guest binary.
          adbShell(
              '/vendor/bin/cuttlefish_sensor_injection hinge_angle ' +
              states[index].hinge_angle_value);
        }
        // Update the Device Details view.
        updateDeviceStateDetails();
        // Cycle to the next state.
        index = (index + 1) % states.length;
      }
    }
  }

  function startMouseTracking() {
    let deviceDisplayList = document.getElementsByClassName("device-display");
    if (window.PointerEvent) {
      for (const deviceDisplay of deviceDisplayList) {
        deviceDisplay.addEventListener('pointerdown', onStartDrag);
        deviceDisplay.addEventListener('pointermove', onContinueDrag);
        deviceDisplay.addEventListener('pointerup', onEndDrag);
      }
    } else if (window.TouchEvent) {
      for (const deviceDisplay of deviceDisplayList) {
        deviceDisplay.addEventListener('touchstart', onStartDrag);
        deviceDisplay.addEventListener('touchmove', onContinueDrag);
        deviceDisplay.addEventListener('touchend', onEndDrag);
      }
    } else if (window.MouseEvent) {
      for (const deviceDisplay of deviceDisplayList) {
        deviceDisplay.addEventListener('mousedown', onStartDrag);
        deviceDisplay.addEventListener('mousemove', onContinueDrag);
        deviceDisplay.addEventListener('mouseup', onEndDrag);
      }
    }
  }

  function stopMouseTracking() {
    let deviceDisplayList = document.getElementsByClassName("device-display");
    if (window.PointerEvent) {
      for (const deviceDisplay of deviceDisplayList) {
        deviceDisplay.removeEventListener('pointerdown', onStartDrag);
        deviceDisplay.removeEventListener('pointermove', onContinueDrag);
        deviceDisplay.removeEventListener('pointerup', onEndDrag);
      }
    } else if (window.TouchEvent) {
      for (const deviceDisplay of deviceDisplayList) {
        deviceDisplay.removeEventListener('touchstart', onStartDrag);
        deviceDisplay.removeEventListener('touchmove', onContinueDrag);
        deviceDisplay.removeEventListener('touchend', onEndDrag);
      }
    } else if (window.MouseEvent) {
      for (const deviceDisplay of deviceDisplayList) {
        deviceDisplay.removeEventListener('mousedown', onStartDrag);
        deviceDisplay.removeEventListener('mousemove', onContinueDrag);
        deviceDisplay.removeEventListener('mouseup', onEndDrag);
      }
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

    // The <video> element:
    const deviceDisplay = e.target;

    // Before the first video frame arrives there is no way to know width and
    // height of the device's screen, so turn every click into a click at 0x0.
    // A click at that position is not more dangerous than anywhere else since
    // the user is clicking blind anyways.
    const videoWidth = deviceDisplay.videoWidth? deviceDisplay.videoWidth: 1;
    const videoHeight = deviceDisplay.videoHeight? deviceDisplay.videoHeight: 1;
    const elementWidth = deviceDisplay.offsetWidth? deviceDisplay.offsetWidth: 1;
    const elementHeight = deviceDisplay.offsetHeight? deviceDisplay.offsetHeight: 1;

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

    const display_label = deviceDisplay.id;

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
  document.getElementById('device-selector').style.display = 'none';
  // Show the device control screen
  document.getElementById('device-connection').style.visibility = 'visible';
  ConnectToDevice(dev_id);
}

function ShowNewDeviceList(device_ids) {
  let ul = document.getElementById('device-list');
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
document.getElementById('refresh-list')
    .addEventListener('click', evt => UpdateDeviceList());
