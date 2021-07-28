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

function showDeviceListUI() {
  document.getElementById('error-message').style.display = 'none';
  // Hide the device control screen
  document.getElementById('device-connection').style.visibility = 'none';
  // Show the device selection screen
  document.getElementById('device-selector').style.display = 'visible';
}

function showDeviceControlUI() {
  document.getElementById('error-message').style.display = 'none';
  // Hide the device selection screen
  document.getElementById('device-selector').style.display = 'none';
  // Show the device control screen
  document.getElementById('device-connection').style.visibility = 'visible';
}

function websocketUrl(path) {
  return ((location.protocol == 'http:') ? 'ws:' : 'wss:') + location.host +
      '/' + path;
}

async function ConnectDevice(deviceId) {
  console.log('Connect: ' + deviceId);
  // Prepare messages in case of connection failure
  let connectionAttemptDuration = 0;
  const intervalMs = 15000;
  let connectionInterval = setInterval(() => {
    connectionAttemptDuration += intervalMs;
    if (connectionAttemptDuration > 30000) {
      showError(
          'Connection should have occurred by now. ' +
          'Please attempt to restart the guest device.');
      clearInterval(connectionInterval);
    } else if (connectionAttemptDuration > 15000) {
      showWarning('Connection is taking longer than expected');
    }
  }, intervalMs);

  let options = {
    wsUrl: websocketUrl('connect_client'),
  };

  let module = await import('./cf_webrtc.js');
  let deviceConnection = await module.Connect(deviceId, options);
  clearInterval(connectionInterval);
  return deviceConnection;
}

function showWarning(msg) {
  let element = document.getElementById('error-message');
  element.className = 'warning';
  element.textContent = msg;
  element.style.visibility = 'visible';
}

function showError(msg) {
  let element = document.getElementById('error-message');
  element.className = 'error';
  element.textContent = msg;
  element.style.visibility = 'visible';
}

class DeviceListApp {
  #websocketUrl;
  #selectDeviceCb;

  constructor({websocketUrl, selectDeviceCb}) {
    this.#websocketUrl = websocketUrl;
    this.#selectDeviceCb = selectDeviceCb;
  }

  start() {
    // Get any devices that are already connected
    this.#UpdateDeviceList();

    // Update the list at the user's request
    document.getElementById('refresh-list')
        .addEventListener('click', evt => this.#UpdateDeviceList());
  }

  #UpdateDeviceList() {
    let ws = new WebSocket(this.#websocketUrl);
    ws.onopen = () => {
      ws.send('give me those device ids');
    };
    ws.onmessage = msg => {
      let device_ids = JSON.parse(msg.data);
      this.#ShowNewDeviceList(device_ids);
    };
  }

  #ShowNewDeviceList(device_ids) {
    let ul = document.getElementById('device-list');
    ul.innerHTML = '';
    let count = 1;
    let device_to_button_map = {};
    for (const dev_id of device_ids) {
      const button_id = 'connect_' + count++;
      ul.innerHTML +=
          ('<li class="device_entry" title="Connect to ' + dev_id +
           '"><div><span>' + dev_id + '</span><button id="' + button_id +
           '" >Connect</button></div></li>');
      device_to_button_map[dev_id] = button_id;
    }

    for (const [dev_id, button_id] of Object.entries(device_to_button_map)) {
      let button = document.getElementById(button_id);
      button.addEventListener('click', evt => {
        let button = $(evt.target);
        let div = button.parent();
        button.remove();
        div.append('<span class="spinner material-icons">sync</span>');
        this.#selectDeviceCb(dev_id);
      });
    }
  }
}  // DeviceListApp

class DeviceDetailsUpdater {
  #element;

  constructor() {
    this.#element = document.getElementById('device-details-hardware');
  }

  setHardwareDetailsText(text) {
    this.#element.dataset.HardwareDetailsText = text;
    return this;
  }

  setDeviceStateDetailsText(text) {
    this.#element.dataset.deviceStateDetailsText = text;
    return this;
  }

  setDisplayDetailsText(text) {
    this.#element.dataset.displayDetailsText = text;
    return this;
  }

  update() {
    this.#element.textContent =
        [
          this.#element.dataset.hardwareDetailsText,
          this.#element.dataset.deviceStateDetailsText,
          this.#element.dataset.displayDetailsText,
        ].filter(e => e /*remove empty*/)
            .join('\n');
  }
}  // DeviceDetailsUpdater

class DeviceControlApp {
  #deviceConnection = {};
  #currentRotation = 0;
  #displayDescriptions = [];
  #buttons = {};

  constructor(deviceConnection) {
    this.#deviceConnection = deviceConnection;
  }

  start() {
    console.log('Device description: ', this.#deviceConnection.description);
    this.#deviceConnection.onControlMessage(msg => this.#onControlMessage(msg));
    let keyboardCaptureCtrl = createToggleControl(
        document.getElementById('keyboard-capture-control'), 'keyboard');
    let micCaptureCtrl = createToggleControl(
        document.getElementById('mic-capture-control'), 'mic');
    let cameraCtrl = createToggleControl(
        document.getElementById('camera-control'), 'videocam');

    keyboardCaptureCtrl.OnClick(
        enabled => this.#onKeyboardCaptureToggle(enabled));
    micCaptureCtrl.OnClick(enabled => this.#onMicCaptureToggle(enabled));
    cameraCtrl.OnClick(enabled => this.#onVideoCaptureToggle(enabled));

    this.#UIChangesOnConnected();
  }

  #UIChangesOnConnected() {
    window.onresize = evt => this.#resizeDeviceDisplays();
    // Set up control panel buttons
    this.#buttons = {};
    this.#buttons['power'] = createControlPanelButton(
        'power', 'Power', 'power_settings_new',
        evt => this.#onControlPanelButton(evt));
    this.#buttons['home'] = createControlPanelButton(
        'home', 'Home', 'home', evt => this.#onControlPanelButton(evt));
    this.#buttons['menu'] = createControlPanelButton(
        'menu', 'Menu', 'menu', evt => this.#onControlPanelButton(evt));
    this.#buttons['rotate'] = createControlPanelButton(
        'rotate', 'Rotate', 'screen_rotation',
        evt => this.#onRotateButton(evt));
    this.#buttons['rotate'].adb = true;
    this.#buttons['volumemute'] = createControlPanelButton(
        'volumemute', 'Volume Mute', 'volume_mute',
        evt => this.#onControlPanelButton(evt));
    this.#buttons['volumedown'] = createControlPanelButton(
        'volumedown', 'Volume Down', 'volume_down',
        evt => this.#onControlPanelButton(evt));
    this.#buttons['volumeup'] = createControlPanelButton(
        'volumeup', 'Volume Up', 'volume_up',
        evt => this.#onControlPanelButton(evt));
    createModalButton(
        'device-details-button', 'device-details-modal',
        'device-details-close');
    createModalButton(
        'bluetooth-console-button', 'bluetooth-console-modal',
        'bluetooth-console-close');

    if (this.#deviceConnection.description.custom_control_panel_buttons.length >
        0) {
      document.getElementById('control-panel-custom-buttons').style.display =
          'flex';
      for (const button of this.#deviceConnection.description
               .custom_control_panel_buttons) {
        if (button.shell_command) {
          // This button's command is handled by sending an ADB shell command.
          this.#buttons[button.command] = createControlPanelButton(
              button.command, button.title, button.icon_name,
              e => this.#onCustomShellButton(button.shell_command, e),
              'control-panel-custom-buttons');
          this.#buttons[button.command].adb = true;
        } else if (button.device_states) {
          // This button corresponds to variable hardware device state(s).
          this.#buttons[button.command] = createControlPanelButton(
              button.command, button.title, button.icon_name,
              this.#getCustomDeviceStateButtonCb(button.device_states),
              'control-panel-custom-buttons');
          for (const device_state of button.device_states) {
            // hinge_angle is currently injected via an adb shell command that
            // triggers a guest binary.
            if ('hinge_angle_value' in device_state) {
              this.#buttons[button.command].adb = true;
            }
          }
        } else {
          // This button's command is handled by custom action server.
          this.#buttons[button.command] = createControlPanelButton(
              button.command, button.title, button.icon_name,
              evt => this.#onControlPanelButton(evt),
              'control-panel-custom-buttons');
        }
      }
    }

    // Set up displays
    this.#createDeviceDisplays();

    // Set up audio
    const deviceAudio = document.getElementById('device-audio');
    for (const audio_desc of this.#deviceConnection.description.audio_streams) {
      let stream_id = audio_desc.stream_id;
      this.#deviceConnection.getStream(stream_id)
          .then(stream => {
            deviceAudio.srcObject = stream;
          })
          .catch(e => console.error('Unable to get audio stream: ', e));
    }

    // Set up touch input
    this.#startMouseTracking();

    this.#updateDeviceHardwareDetails(
        this.#deviceConnection.description.hardware);
    this.#updateDeviceDisplayDetails(
        this.#deviceConnection.description.displays[0]);

    // Show the error message and disable buttons when the WebRTC connection
    // fails.
    this.#deviceConnection.onConnectionStateChange(state => {
      if (state == 'disconnected' || state == 'failed') {
        this.#showWebrtcError();
      }
    });

    let bluetoothConsole =
        cmdConsole('bluetooth-console-view', 'bluetooth-console-input');
    bluetoothConsole.addCommandListener(cmd => {
      let inputArr = cmd.split(' ');
      let command = inputArr[0];
      inputArr.shift();
      let args = inputArr;
      this.#deviceConnection.sendBluetoothMessage(
          createRootcanalMessage(command, args));
    });
    this.#deviceConnection.onBluetoothMessage(msg => {
      bluetoothConsole.addLine(decodeRootcanalMessage(msg));
    });
  }

  #showWebrtcError() {
    document.getElementById('status-message').className = 'error';
    document.getElementById('status-message').textContent =
        'No connection to the guest device. ' +
        'Please ensure the WebRTC process on the host machine is active.';
    document.getElementById('status-message').style.visibility = 'visible';
    const deviceDisplays = document.getElementById('device-displays');
    deviceDisplays.style.display = 'none';
    for (const [_, button] of Object.entries(this.#buttons)) {
      button.disabled = true;
    }
  }

  #takePhoto() {
    const imageCapture = this.#deviceConnection.imageCapture;
    if (imageCapture) {
      const photoSettings = {
        imageWidth: this.#deviceConnection.cameraWidth,
        imageHeight: this.#deviceConnection.cameraHeight
      };
      imageCapture.takePhoto(photoSettings)
          .then(blob => blob.arrayBuffer())
          .then(buffer => this.#deviceConnection.sendOrQueueCameraData(buffer))
          .catch(error => console.log(error));
    }
  }

  #getCustomDeviceStateButtonCb(device_states) {
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
        this.#deviceConnection.sendControlMessage(JSON.stringify(message));
        console.log(JSON.stringify(message));
        let lidSwitchOpen = null;
        if ('lid_switch_open' in states[index]) {
          lidSwitchOpen = states[index].lid_switch_open;
        }
        let hingeAngle = null;
        if ('hinge_angle_value' in states[index]) {
          hingeAngle = states[index].hinge_angle_value;
          // TODO(b/181157794): Use a custom Sensor HAL for hinge_angle
          // injection instead of this guest binary.
          adbShell(
              '/vendor/bin/cuttlefish_sensor_injection hinge_angle ' +
              states[index].hinge_angle_value);
        }
        // Update the Device Details view.
        this.#updateDeviceStateDetails(lidSwitchOpen, hingeAngle);
        // Cycle to the next state.
        index = (index + 1) % states.length;
      }
    }
  }

  #resizeDeviceDisplays() {
    const deviceDisplayPadding = 10;

    let deviceDisplayList = document.getElementsByClassName('device-display');
    let deviceDisplayVideoList =
        document.getElementsByClassName('device-display-video');

    const deviceDisplays = document.getElementById('device-displays');
    const rotationDegrees = this.#getTransformRotation(deviceDisplays);
    const rotationRadians = rotationDegrees * Math.PI / 180;

    // Auto-scale the screen based on window size.
    let availableWidth = deviceDisplays.clientWidth;
    let availableHeight = deviceDisplays.clientHeight;

    // Reserve space for padding between the displays.
    availableWidth = availableWidth -
        (this.#displayDescriptions.length * deviceDisplayPadding);

    // Loop once over all of the displays to compute the total space needed.
    let neededWidth = 0;
    let neededHeight = 0;
    for (let i = 0; i < deviceDisplayList.length; i++) {
      let deviceDisplayDescription = this.#displayDescriptions[i];
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

    const scaling =
        Math.min(availableWidth / neededWidth, availableHeight / neededHeight);

    // Loop again over all of the displays to set the sizes and positions.
    let deviceDisplayLeftOffset = 0;
    for (let i = 0; i < deviceDisplayList.length; i++) {
      let deviceDisplay = deviceDisplayList[i];
      let deviceDisplayVideo = deviceDisplayVideoList[i];
      let deviceDisplayDescription = this.#displayDescriptions[i];

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
      deviceDisplayVideo.style.transform = `translateX(${offsetX}px) ` +
          `translateY(${offsetY}px) ` +
          `rotateZ(${rotationDegrees}deg) `;

      deviceDisplay.style.left = `${deviceDisplayLeftOffset}px`;
      deviceDisplay.style.width = scaledBoundingBoxWidth;
      deviceDisplay.style.height = scaledBoundingBoxHeight;

      deviceDisplayLeftOffset = deviceDisplayLeftOffset + deviceDisplayPadding +
          scaledBoundingBoxWidth;
    }
  }

  #getTransformRotation(element) {
    if (!element.style.textIndent) {
      return 0;
    }
    // Remove 'px' and convert to float.
    return parseFloat(element.style.textIndent.slice(0, -2));
  }

  #onControlMessage(message) {
    let message_data = JSON.parse(message.data);
    console.log(message_data)
    let metadata = message_data.metadata;
    if (message_data.event == 'VIRTUAL_DEVICE_BOOT_STARTED') {
      // Start the adb connection after receiving the BOOT_STARTED message.
      // (This is after the adbd start message. Attempting to connect
      // immediately after adbd starts causes issues.)
      this.#initializeAdb();
    }
    if (message_data.event == 'VIRTUAL_DEVICE_SCREEN_CHANGED') {
      if (metadata.rotation != this.#currentRotation) {
        // Animate the screen rotation.
        const targetRotation = metadata.rotation == 0 ? 0 : -90;

        $('#device-displays')
            .animate(
                {
                  textIndent: targetRotation,
                },
                {
                  duration: 1000,
                  step: (now, tween) => {
                    this.#resizeDeviceDisplays();
                  },
                });
      }

      this.#currentRotation = metadata.rotation;
      this.#updateDeviceDisplayDetails(
          {dpi: metadata.dpi, x_res: metadata.width, y_res: metadata.height});
    }
    if (message_data.event == 'VIRTUAL_DEVICE_CAPTURE_IMAGE') {
      if (this.$deviceConnection.cameraEnabled) {
        this.#takePhoto();
      }
    }
  }

  #updateDeviceStateDetails(lidSwitchOpen, hingeAngle) {
    let deviceStateDetailsTextLines = [];
    if (lidSwitchOpen != null) {
      let state = lidSwitchOpen ? 'Opened' : 'Closed';
      deviceStateDetailsTextLines.push(`Lid Switch - ${state}`);
    }
    if (hingeAngle != null) {
      deviceStateDetailsTextLines.push(`Hinge Angle - ${hingeAngle}`);
    }
    let deviceStateDetailsText = deviceStateDetailsTextLines.join('\n');
    new DeviceDetailsUpdater()
        .setDeviceStateDetailsText(deviceStateDetailsText)
        .update();
  }

  #updateDeviceHardwareDetails(hardware) {
    let hardwareDetailsTextLines = [];
    Object.keys(hardware).forEach((key) => {
      let value = hardware[key];
      hardwareDetailsTextLines.push(`${key} - ${value}`);
    });

    let hardwareDetailsText = hardwareDetailsTextLines.join('\n');
    new DeviceDetailsUpdater()
        .setHardwareDetailsText(hardwareDetailsText)
        .update();
  }

  #updateDeviceDisplayDetails(display) {
    let dpi = display.dpi;
    let x_res = display.x_res;
    let y_res = display.y_res;
    let rotated = this.#currentRotation == 1 ? ' (Rotated)' : '';
    let displayDetailsText =
        `Display - ${x_res}x${y_res} (${dpi}DPI)${rotated}`;
    new DeviceDetailsUpdater()
        .setDisplayDetailsText(displayDetailsText)
        .update();
  }

  // Creates a <video> element and a <div> container element for each display.
  // The extra <div> container elements are used to maintain the width and
  // height of the device as the CSS 'transform' property used on the <video>
  // element for rotating the device only affects the visuals of the element
  // and not its layout.
  #createDeviceDisplays() {
    console.log('description: ', this.#deviceConnection.description.displays);
    this.#displayDescriptions = this.#deviceConnection.description.displays;
    let anyDisplayLoaded = false;
    const deviceDisplays = document.getElementById('device-displays');
    for (const deviceDisplayDescription of this.#displayDescriptions) {
      console.log('description: ', deviceDisplayDescription);
      let deviceDisplay = document.createElement('div');
      deviceDisplay.classList.add('device-display');
      // Start the screen as hidden. Only show when data is ready.
      deviceDisplay.style.visibility = 'hidden';

      let deviceDisplayVideo = document.createElement('video');
      deviceDisplayVideo.autoplay = true;
      deviceDisplayVideo.id = deviceDisplayDescription.stream_id;
      deviceDisplayVideo.classList.add('device-display-video');

      deviceDisplayVideo.addEventListener('loadeddata', (evt) => {
        if (!anyDisplayLoaded) {
          anyDisplayLoaded = true;
          this.#onDeviceDisplayLoaded();
        }
      });

      deviceDisplay.appendChild(deviceDisplayVideo);
      deviceDisplays.appendChild(deviceDisplay);

      let stream_id = deviceDisplayDescription.stream_id;
      this.#deviceConnection.getStream(stream_id)
          .then(stream => {
            deviceDisplayVideo.srcObject = stream;
          })
          .catch(e => console.error('Unable to get display stream: ', e));
    }
  }

  #initializeAdb() {
    init_adb(this.#deviceConnection, () => this.#showAdbConnected(), () => {
      document.getElementById('status-message').className = 'error';
      document.getElementById('status-message').textContent =
          'adb connection failed.';
      document.getElementById('status-message').style.visibility = 'visible';
      for (const [_, button] of Object.entries(this.#buttons)) {
        if (button.adb) {
          button.disabled = true;
        }
      }
    });
  }

  #showAdbConnected() {
    // Screen changed messages are not reported until after boot has completed.
    // Certain default adb buttons change screen state, so wait for boot
    // completion before enabling these buttons.
    document.getElementById('status-message').className = 'connected';
    document.getElementById('status-message').textContent =
        'adb connection established successfully.';
    setTimeout(() => {
      document.getElementById('status-message').style.visibility = 'hidden';
    }, 5000);
    for (const [_, button] of Object.entries(this.#buttons)) {
      if (button.adb) {
        button.disabled = false;
      }
    }
  }

  #onDeviceDisplayLoaded() {
    document.getElementById('status-message').textContent =
        'Awaiting bootup and adb connection. Please wait...';
    this.#resizeDeviceDisplays();

    let deviceDisplayList = document.getElementsByClassName('device-display');
    for (const deviceDisplay of deviceDisplayList) {
      deviceDisplay.style.visibility = 'visible';
    }

    // Enable the buttons after the screen is visible.
    for (const [key, button] of Object.entries(this.#buttons)) {
      if (!button.adb) {
        button.disabled = false;
      }
    }
    // Start the adb connection if it is not already started.
    this.#initializeAdb();
  }

  #onRotateButton(e) {
    // Attempt to init adb again, in case the initial connection failed.
    // This succeeds immediately if already connected.
    this.#initializeAdb();
    if (e.type == 'mousedown') {
      adbShell(
          '/vendor/bin/cuttlefish_sensor_injection rotate ' +
          (this.#currentRotation == 0 ? 'landscape' : 'portrait'))
    }
  }

  #onControlPanelButton(e) {
    if (e.type == 'mouseout' && e.which == 0) {
      // Ignore mouseout events if no mouse button is pressed.
      return;
    }
    this.#deviceConnection.sendControlMessage(JSON.stringify({
      command: e.target.dataset.command,
      button_state: e.type == 'mousedown' ? 'down' : 'up',
    }));
  }

  #onKeyboardCaptureToggle(enabled) {
    if (enabled) {
      document.addEventListener('keydown', evt => this.#onKeyEvent(evt));
      document.addEventListener('keyup', evt => this.#onKeyEvent(evt));
    } else {
      document.removeEventListener('keydown', evt => this.#onKeyEvent(evt));
      document.removeEventListener('keyup', evt => this.#onKeyEvent(evt));
    }
  }

  #onKeyEvent(e) {
    e.preventDefault();
    this.#deviceConnection.sendKeyEvent(e.code, e.type);
  }

  #startMouseTracking() {
    let $this = this;
    let mouseIsDown = false;
    let mouseCtx = {
      down: false,
      touchIdSlotMap: new Map(),
      touchSlots: [],
    };
    function onStartDrag(e) {
      e.preventDefault();

      // console.log("mousedown at " + e.pageX + " / " + e.pageY);
      mouseCtx.down = true;

      $this.#sendEventUpdate(mouseCtx, e);
    }

    function onEndDrag(e) {
      e.preventDefault();

      // console.log("mouseup at " + e.pageX + " / " + e.pageY);
      mouseCtx.down = false;

      $this.#sendEventUpdate(mouseCtx, e);
    }

    function onContinueDrag(e) {
      e.preventDefault();

      // console.log("mousemove at " + e.pageX + " / " + e.pageY + ", down=" +
      // mouseIsDown);
      if (mouseCtx.down) {
        $this.#sendEventUpdate(mouseCtx, e);
      }
    }

    let deviceDisplayList = document.getElementsByClassName('device-display');
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

  #sendEventUpdate(ctx, e) {
    let eventType = e.type.substring(0, 5);

    // The <video> element:
    const deviceDisplay = e.target;

    // Before the first video frame arrives there is no way to know width and
    // height of the device's screen, so turn every click into a click at 0x0.
    // A click at that position is not more dangerous than anywhere else since
    // the user is clicking blind anyways.
    const videoWidth = deviceDisplay.videoWidth ? deviceDisplay.videoWidth : 1;
    const videoHeight =
        deviceDisplay.videoHeight ? deviceDisplay.videoHeight : 1;
    const elementWidth =
        deviceDisplay.offsetWidth ? deviceDisplay.offsetWidth : 1;
    const elementHeight =
        deviceDisplay.offsetHeight ? deviceDisplay.offsetHeight : 1;

    // vh*ew > eh*vw? then scale h instead of w
    const scaleHeight = videoHeight * elementWidth > videoWidth * elementHeight;
    let elementScaling = 0, videoScaling = 0;
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
    const scaling =
        scaleWidth ? videoWidth / elementWidth : videoHeight / elementHeight;

    let xArr = [];
    let yArr = [];
    let idArr = [];
    let slotArr = [];

    if (eventType == 'mouse' || eventType == 'point') {
      xArr.push(e.offsetX);
      yArr.push(e.offsetY);

      let thisId = -1;
      if (eventType == 'point') {
        thisId = e.pointerId;
      }

      slotArr.push(0);
      idArr.push(thisId);
    } else if (eventType == 'touch') {
      // touchstart: list of touch points that became active
      // touchmove: list of touch points that changed
      // touchend: list of touch points that were removed
      let changes = e.changedTouches;
      let rect = e.target.getBoundingClientRect();
      for (let i = 0; i < changes.length; i++) {
        xArr.push(changes[i].pageX - rect.left);
        yArr.push(changes[i].pageY - rect.top);
        if (ctx.touchIdSlotMap.has(changes[i].identifier)) {
          let slot = ctx.touchIdSlotMap.get(changes[i].identifier);

          slotArr.push(slot);
          if (e.type == 'touchstart') {
            // error
            console.error('touchstart when already have slot');
            return;
          } else if (e.type == 'touchmove') {
            idArr.push(changes[i].identifier);
          } else if (e.type == 'touchend') {
            ctx.touchSlots[slot] = false;
            ctx.touchIdSlotMap.delete(changes[i].identifier);
            idArr.push(-1);
          }
        } else {
          if (e.type == 'touchstart') {
            let slot = -1;
            for (let j = 0; j < ctx.touchSlots.length; j++) {
              if (!ctx.touchSlots[j]) {
                slot = j;
                break;
              }
            }
            if (slot == -1) {
              slot = ctx.touchSlots.length;
              ctx.touchSlots.push(true);
            }
            slotArr.push(slot);
            ctx.touchSlots[slot] = true;
            ctx.touchIdSlotMap.set(changes[i].identifier, slot);
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

    for (let i = 0; i < xArr.length; i++) {
      xArr[i] = xArr[i] * scaling;
      yArr[i] = yArr[i] * scaling;

      // Substract the offset produced by the difference in aspect ratio, if
      // any.
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

    this.#deviceConnection.sendMultiTouch(
        {idArr, xArr, yArr, down: ctx.down, slotArr, display_label});
  }

  #onMicCaptureToggle(enabled) {
    this.#deviceConnection.useMic(enabled);
  }

  #onVideoCaptureToggle(enabled) {
    this.#deviceConnection.useVideo(enabled);
  }

  #onCustomShellButton(shell_command, e) {
    // Attempt to init adb again, in case the initial connection failed.
    // This succeeds immediately if already connected.
    this.#initializeAdb();
    if (e.type == 'mousedown') {
      adbShell(shell_command);
    }
  }
}  // DeviceControlApp


// The app starts by showing the device list
showDeviceListUI();
let listDevicesUrl = websocketUrl('list_devices');
let selectDeviceCb = deviceId => {
  ConnectDevice(deviceId).then(
      deviceConnection => {
        let deviceControlApp = new DeviceControlApp(deviceConnection);
        deviceControlApp.start();
        showDeviceControlUI();
      },
      err => {
        console.error('Unable to connect: ', err);
        showError(
            'No connection to the guest device. ' +
            'Please ensure the WebRTC process on the host machine is active.');
      });
};
let deviceListApp =
    new DeviceListApp({websocketUrl: listDevicesUrl, selectDeviceCb});
deviceListApp.start();
