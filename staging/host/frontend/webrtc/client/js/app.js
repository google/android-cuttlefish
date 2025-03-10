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

async function ConnectDevice(deviceId, serverConnector) {
  console.debug('Connect: ' + deviceId);
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

  let module = await import('./cf_webrtc.js');
  let deviceConnection = await module.Connect(deviceId, serverConnector);
  console.info('Connected to ' + deviceId);
  clearInterval(connectionInterval);
  return deviceConnection;
}

function setupMessages() {
  let closeBtn = document.querySelector('#error-message .close-btn');
  closeBtn.addEventListener('click', evt => {
    evt.target.parentElement.className = 'hidden';
  });
}

function showMessage(msg, className, duration) {
  let element = document.getElementById('error-message');
  let previousTimeout = element.dataset.timeout;
  if (previousTimeout !== undefined) {
    clearTimeout(previousTimeout);
  }
  if (element.childNodes.length < 2) {
    // First time, no text node yet
    element.insertAdjacentText('afterBegin', msg);
  } else {
    element.childNodes[0].data = msg;
  }
  element.className = className;

  if (duration !== undefined) {
    element.dataset.timeout = setTimeout(() => {
      element.className = 'hidden';
    }, duration);
  }
}

function showInfo(msg, duration) {
  showMessage(msg, 'info', duration);
}

function showWarning(msg, duration) {
  showMessage(msg, 'warning', duration);
}

function showError(msg, duration) {
  showMessage(msg, 'error', duration);
}


class DeviceDetailsUpdater {
  #element;

  constructor() {
    this.#element = document.getElementById('device-details-hardware');
  }

  setHardwareDetailsText(text) {
    this.#element.dataset.hardwareDetailsText = text;
    return this;
  }

  setDeviceStateDetailsText(text) {
    this.#element.dataset.deviceStateDetailsText = text;
    return this;
  }

  update() {
    this.#element.textContent =
        [
          this.#element.dataset.hardwareDetailsText,
          this.#element.dataset.deviceStateDetailsText,
        ].filter(e => e /*remove empty*/)
            .join('\n');
  }
}  // DeviceDetailsUpdater

class DeviceControlApp {
  #deviceConnection = {};
  #currentRotation = 0;
  #currentScreenStyles = {};
  #displayDescriptions = [];
  #recording = {};
  #phys = {};
  #deviceCount = 0;
  #micActive = false;
  #adbConnected = false;

  constructor(deviceConnection) {
    this.#deviceConnection = deviceConnection;
  }

  start() {
    console.debug('Device description: ', this.#deviceConnection.description);
    this.#deviceConnection.onControlMessage(msg => this.#onControlMessage(msg));
    createToggleControl(
        document.getElementById('camera_off_btn'),
        enabled => this.#onCameraCaptureToggle(enabled));
    createToggleControl(
        document.getElementById('record_video_btn'),
        enabled => this.#onVideoCaptureToggle(enabled));
    const audioElm = document.getElementById('device-audio');

    let audioPlaybackCtrl = createToggleControl(
        document.getElementById('volume_off_btn'),
        enabled => this.#onAudioPlaybackToggle(enabled), !audioElm.paused);
    // The audio element may start or stop playing at any time, this ensures the
    // audio control always show the right state.
    audioElm.onplay = () => audioPlaybackCtrl.Set(true);
    audioElm.onpause = () => audioPlaybackCtrl.Set(false);

    this.#showDeviceUI();
  }

  #showDeviceUI() {
    // Set up control panel buttons
    addMouseListeners(
        document.querySelector('#power_btn'),
        evt => this.#onControlPanelButton(evt, 'power'));
    addMouseListeners(
        document.querySelector('#back_btn'),
        evt => this.#onControlPanelButton(evt, 'back'));
    addMouseListeners(
        document.querySelector('#home_btn'),
        evt => this.#onControlPanelButton(evt, 'home'));
    addMouseListeners(
        document.querySelector('#menu_btn'),
        evt => this.#onControlPanelButton(evt, 'menu'));
    addMouseListeners(
        document.querySelector('#rotate_left_btn'),
        evt => this.#onRotateLeftButton(evt, 'rotate'));
    addMouseListeners(
        document.querySelector('#rotate_right_btn'),
        evt => this.#onRotateRightButton(evt, 'rotate'));
    addMouseListeners(
        document.querySelector('#volume_up_btn'),
        evt => this.#onControlPanelButton(evt, 'volumeup'));
    addMouseListeners(
        document.querySelector('#volume_down_btn'),
        evt => this.#onControlPanelButton(evt, 'volumedown'));
    addMouseListeners(
        document.querySelector('#mic_btn'), evt => this.#onMicButton(evt));

    createModalButton(
        'device-details-button', 'device-details-modal',
        'device-details-close');
    createModalButton(
        'bluetooth-modal-button', 'bluetooth-prompt', 'bluetooth-prompt-close');
    createModalButton(
        'bluetooth-prompt-wizard', 'bluetooth-wizard', 'bluetooth-wizard-close',
        'bluetooth-prompt');
    createModalButton(
        'bluetooth-wizard-device', 'bluetooth-wizard-confirm',
        'bluetooth-wizard-confirm-close', 'bluetooth-wizard');
    createModalButton(
        'bluetooth-wizard-another', 'bluetooth-wizard',
        'bluetooth-wizard-close', 'bluetooth-wizard-confirm');
    createModalButton(
        'bluetooth-prompt-list', 'bluetooth-list', 'bluetooth-list-close',
        'bluetooth-prompt');
    createModalButton(
        'bluetooth-prompt-console', 'bluetooth-console',
        'bluetooth-console-close', 'bluetooth-prompt');
    createModalButton(
        'bluetooth-wizard-cancel', 'bluetooth-prompt', 'bluetooth-wizard-close',
        'bluetooth-wizard');

    positionModal('device-details-button', 'bluetooth-modal');
    positionModal('device-details-button', 'bluetooth-prompt');
    positionModal('device-details-button', 'bluetooth-wizard');
    positionModal('device-details-button', 'bluetooth-wizard-confirm');
    positionModal('device-details-button', 'bluetooth-list');
    positionModal('device-details-button', 'bluetooth-console');

    createButtonListener('bluetooth-prompt-list', null, this.#deviceConnection,
      evt => this.#onRootCanalCommand(this.#deviceConnection, "list", evt));
    createButtonListener('bluetooth-wizard-device', null, this.#deviceConnection,
      evt => this.#onRootCanalCommand(this.#deviceConnection, "add", evt));
    createButtonListener('bluetooth-list-trash', null, this.#deviceConnection,
      evt => this.#onRootCanalCommand(this.#deviceConnection, "del", evt));
    createButtonListener('bluetooth-prompt-wizard', null, this.#deviceConnection,
      evt => this.#onRootCanalCommand(this.#deviceConnection, "list", evt));
    createButtonListener('bluetooth-wizard-another', null, this.#deviceConnection,
      evt => this.#onRootCanalCommand(this.#deviceConnection, "list", evt));

    if (this.#deviceConnection.description.custom_control_panel_buttons.length >
        0) {
      document.getElementById('control-panel-custom-buttons').style.display =
          'flex';
      for (const button of this.#deviceConnection.description
               .custom_control_panel_buttons) {
        if (button.shell_command) {
          // This button's command is handled by sending an ADB shell command.
          let element = createControlPanelButton(
              button.title, button.icon_name,
              e => this.#onCustomShellButton(button.shell_command, e),
              'control-panel-custom-buttons');
          element.dataset.adb = true;
        } else if (button.device_states) {
          // This button corresponds to variable hardware device state(s).
          let element = createControlPanelButton(
              button.title, button.icon_name,
              this.#getCustomDeviceStateButtonCb(button.device_states),
              'control-panel-custom-buttons');
          for (const device_state of button.device_states) {
            // hinge_angle is currently injected via an adb shell command that
            // triggers a guest binary.
            if ('hinge_angle_value' in device_state) {
              element.dataset.adb = true;
            }
          }
        } else {
          // This button's command is handled by custom action server.
          createControlPanelButton(
              button.title, button.icon_name,
              evt => this.#onControlPanelButton(evt, button.command),
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
            deviceAudio.play();
          })
          .catch(e => console.error('Unable to get audio stream: ', e));
    }

    // Set up touch input
    this.#startMouseTracking();

    // Set up keyboard capture
    this.#startKeyboardCapture();

    this.#updateDeviceHardwareDetails(
        this.#deviceConnection.description.hardware);

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
      let decoded = decodeRootcanalMessage(msg);
      let deviceCount = btUpdateDeviceList(decoded);
      if (deviceCount > 0) {
        this.#deviceCount = deviceCount;
        createButtonListener('bluetooth-list-trash', null, this.#deviceConnection,
           evt => this.#onRootCanalCommand(this.#deviceConnection, "del", evt));
      }
      btUpdateAdded(decoded);
      let phyList = btParsePhys(decoded);
      if (phyList) {
        this.#phys = phyList;
      }
      bluetoothConsole.addLine(decoded);
    });
  }

  #onRootCanalCommand(deviceConnection, cmd, evt) {
    if (cmd == "list") {
      deviceConnection.sendBluetoothMessage(createRootcanalMessage("list", []));
    }
    if (cmd == "del") {
      let id = evt.srcElement.getAttribute("data-device-id");
      deviceConnection.sendBluetoothMessage(createRootcanalMessage("del", [id]));
      deviceConnection.sendBluetoothMessage(createRootcanalMessage("list", []));
    }
    if (cmd == "add") {
      let name = document.getElementById('bluetooth-wizard-name').value;
      let type = document.getElementById('bluetooth-wizard-type').value;
      if (type == "remote_loopback") {
        deviceConnection.sendBluetoothMessage(createRootcanalMessage("add", [type]));
      } else {
        let mac = document.getElementById('bluetooth-wizard-mac').value;
        deviceConnection.sendBluetoothMessage(createRootcanalMessage("add", [type, mac]));
      }
      let phyId = this.#phys["LOW_ENERGY"].toString();
      if (type == "remote_loopback") {
        phyId = this.#phys["BR_EDR"].toString();
      }
      let devId = this.#deviceCount.toString();
      this.#deviceCount++;
      deviceConnection.sendBluetoothMessage(createRootcanalMessage("add_device_to_phy", [devId, phyId]));
    }
  }

  #showWebrtcError() {
    showError(
        'No connection to the guest device.  Please ensure the WebRTC' +
        'process on the host machine is active.');
    const deviceDisplays = document.getElementById('device-displays');
    deviceDisplays.style.display = 'none';
    this.#getControlPanelButtons().forEach(b => b.disabled = true);
  }

  #getControlPanelButtons(f) {
    return [...document.querySelectorAll(
        '#control-panel-default-buttons button')];
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
          .catch(error => console.error(error));
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
        console.debug('Control message sent: ', JSON.stringify(message));
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

  #rotateDisplays(rotation) {
    if ((rotation - this.#currentRotation) % 360 == 0) {
      return;
    }

    document.querySelectorAll('.device-display-video').forEach((v, i) => {
      let displayDesc = this.#displayDescriptions[i];
      let aspectRatio = displayDesc.x_res / displayDesc.y_res;

      let keyFrames = [];
      let from = this.#currentScreenStyles[v.id];
      if (from) {
        // If the screen was already rotated, use that state as starting point,
        // otherwise the animation will start at the element's default state.
        keyFrames.push(from);
      }
      let to = getStyleAfterRotation(rotation, aspectRatio);
      keyFrames.push(to);
      v.animate(keyFrames, {duration: 400 /*ms*/, fill: 'forwards'});
      this.#currentScreenStyles[v.id] = to;
    });

    this.#currentRotation = rotation;
    this.#updateDeviceDisplaysInfo();
  }

  #updateDeviceDisplaysInfo() {
    let labels = document.querySelectorAll('.device-display-info');
    labels.forEach((l, i) => {
      let deviceDisplayDescription = this.#displayDescriptions[i];
      let text = `Display ${i} - ` +
          `${deviceDisplayDescription.x_res}x` +
          `${deviceDisplayDescription.y_res} ` +
          `(${deviceDisplayDescription.dpi} DPI)`;
      if (this.#currentRotation != 0) {
        text += ` (Rotated ${this.#currentRotation}deg)`
      }
      l.textContent = text;
    });
  }

  #onControlMessage(message) {
    let message_data = JSON.parse(message.data);
    console.debug('Control message received: ', message_data)
    let metadata = message_data.metadata;
    if (message_data.event == 'VIRTUAL_DEVICE_BOOT_STARTED') {
      // Start the adb connection after receiving the BOOT_STARTED message.
      // (This is after the adbd start message. Attempting to connect
      // immediately after adbd starts causes issues.)
      this.#initializeAdb();
    }
    if (message_data.event == 'VIRTUAL_DEVICE_SCREEN_CHANGED') {
      this.#rotateDisplays(+metadata.rotation);
    }
    if (message_data.event == 'VIRTUAL_DEVICE_CAPTURE_IMAGE') {
      if (this.#deviceConnection.cameraEnabled) {
        this.#takePhoto();
      }
    }
    if (message_data.event == 'VIRTUAL_DEVICE_DISPLAY_POWER_MODE_CHANGED') {
      this.#updateDisplayVisibility(metadata.display, metadata.mode);
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

  // Creates a <video> element and a <div> container element for each display.
  // The extra <div> container elements are used to maintain the width and
  // height of the device as the CSS 'transform' property used on the <video>
  // element for rotating the device only affects the visuals of the element
  // and not its layout.
  #createDeviceDisplays() {
    console.debug(
        'Display descriptions: ', this.#deviceConnection.description.displays);
    this.#displayDescriptions = this.#deviceConnection.description.displays;
    let anyDisplayLoaded = false;
    const deviceDisplays = document.getElementById('device-displays');
    for (const deviceDisplayDescription of this.#displayDescriptions) {
      let displayFragment =
          document.querySelector('#display-template').content.cloneNode(true);

      let deviceDisplayInfo =
          displayFragment.querySelector('.device-display-info');
      deviceDisplayInfo.id = deviceDisplayDescription.stream_id + '_info';

      let deviceDisplayVideo = displayFragment.querySelector('video');
      deviceDisplayVideo.id = deviceDisplayDescription.stream_id;
      deviceDisplayVideo.addEventListener('loadeddata', (evt) => {
        if (!anyDisplayLoaded) {
          anyDisplayLoaded = true;
          this.#onDeviceDisplayLoaded();
        }
      });

      deviceDisplays.appendChild(displayFragment);

      let stream_id = deviceDisplayDescription.stream_id;
      this.#deviceConnection.getStream(stream_id)
          .then(stream => {
            deviceDisplayVideo.srcObject = stream;
          })
          .catch(e => console.error('Unable to get display stream: ', e));
    }
  }

  #initializeAdb() {
    init_adb(
        this.#deviceConnection, () => this.#showAdbConnected(),
        () => this.#showAdbError());
  }

  #showAdbConnected() {
    // Screen changed messages are not reported until after boot has completed.
    // Certain default adb buttons change screen state, so wait for boot
    // completion before enabling these buttons.
    showInfo('adb connection established successfully.', 5000);
    this.#adbConnected = true;
    this.#getControlPanelButtons()
        .filter(b => b.dataset.adb)
        .forEach(b => b.disabled = false);
  }

  #showAdbError() {
    showError('adb connection failed.');
    this.#getControlPanelButtons()
        .filter(b => b.dataset.adb)
        .forEach(b => b.disabled = true);
  }

  #onDeviceDisplayLoaded() {
    if (!this.#adbConnected) {
      // ADB may have connected before, don't show this message in that case
      showInfo('Awaiting bootup and adb connection. Please wait...', 10000);
    }
    this.#updateDeviceDisplaysInfo();

    let deviceDisplayList = document.getElementsByClassName('device-display');
    for (const deviceDisplay of deviceDisplayList) {
      deviceDisplay.style.visibility = 'visible';
    }

    // Enable the buttons after the screen is visible.
    this.#getControlPanelButtons()
        .filter(b => !b.dataset.adb)
        .forEach(b => b.disabled = false);
    // Start the adb connection if it is not already started.
    this.#initializeAdb();
  }

  #onRotateLeftButton(e) {
    if (e.type == 'mousedown') {
      this.#onRotateButton(this.#currentRotation + 90);
    }
  }

  #onRotateRightButton(e) {
    if (e.type == 'mousedown') {
      this.#onRotateButton(this.#currentRotation - 90);
    }
  }

  #onRotateButton(rotation) {
    // Attempt to init adb again, in case the initial connection failed.
    // This succeeds immediately if already connected.
    this.#initializeAdb();
    this.#rotateDisplays(rotation);
    adbShell(`/vendor/bin/cuttlefish_sensor_injection rotate ${rotation}`);
  }

  #onControlPanelButton(e, command) {
    if (e.type == 'mouseout' && e.which == 0) {
      // Ignore mouseout events if no mouse button is pressed.
      return;
    }
    this.#deviceConnection.sendControlMessage(JSON.stringify({
      command: command,
      button_state: e.type == 'mousedown' ? 'down' : 'up',
    }));
  }

  #startKeyboardCapture() {
    const deviceArea = document.querySelector('#device-displays');
    deviceArea.addEventListener('keydown', evt => this.#onKeyEvent(evt));
    deviceArea.addEventListener('keyup', evt => this.#onKeyEvent(evt));
  }

  #onKeyEvent(e) {
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
      // Can't prevent event default behavior to allow the element gain focus
      // when touched and start capturing keyboard input in the parent.
      // console.debug("mousedown at " + e.pageX + " / " + e.pageY);
      mouseCtx.down = true;

      $this.#sendEventUpdate(mouseCtx, e);
    }

    function onEndDrag(e) {
      // Can't prevent event default behavior to allow the element gain focus
      // when touched and start capturing keyboard input in the parent.
      // console.debug("mouseup at " + e.pageX + " / " + e.pageY);
      mouseCtx.down = false;

      $this.#sendEventUpdate(mouseCtx, e);
    }

    function onContinueDrag(e) {
      // Can't prevent event default behavior to allow the element gain focus
      // when touched and start capturing keyboard input in the parent.
      // console.debug("mousemove at " + e.pageX + " / " + e.pageY + ", down=" +
      // mouseIsDown);
      if (mouseCtx.down) {
        $this.#sendEventUpdate(mouseCtx, e);
      }
    }

    let deviceDisplayList = document.getElementsByClassName('device-display-video');
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
    const elementWidth =
        deviceDisplay.offsetWidth ? deviceDisplay.offsetWidth : 1;
    const scaling = videoWidth / elementWidth;

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
      xArr[i] = Math.trunc(xArr[i] * scaling);
      yArr[i] = Math.trunc(yArr[i] * scaling);
    }

    // NOTE: Rotation is handled automatically because the CSS rotation through
    // transforms also rotates the coordinates of events on the object.

    const display_label = deviceDisplay.id;

    this.#deviceConnection.sendMultiTouch(
        {idArr, xArr, yArr, down: ctx.down, slotArr, display_label});
  }

  #updateDisplayVisibility(displayId, powerMode) {
    const display = document.getElementById('display_' + displayId).parentElement;
    if (display == null) {
      console.error('Unknown display id: ' + displayId);
      return;
    }
    powerMode = powerMode.toLowerCase();
    switch (powerMode) {
      case 'on':
        display.style.visibility = 'visible';
        break;
      case 'off':
        display.style.visibility = 'hidden';
        break;
      default:
        console.error('Display ' + displayId + ' has unknown display power mode: ' + powerMode);
    }
  }

  #onMicButton(evt) {
    let nextState = evt.type == 'mousedown';
    if (this.#micActive == nextState) {
      return;
    }
    this.#micActive = nextState;
    this.#deviceConnection.useMic(nextState);
  }

  #onCameraCaptureToggle(enabled) {
    return this.#deviceConnection.useCamera(enabled);
  }

  #getZeroPaddedString(value, desiredLength) {
    const s = String(value);
    return '0'.repeat(desiredLength - s.length) + s;
  }

  #getTimestampString() {
    const now = new Date();
    return [
      now.getFullYear(),
      this.#getZeroPaddedString(now.getMonth(), 2),
      this.#getZeroPaddedString(now.getDay(), 2),
      this.#getZeroPaddedString(now.getHours(), 2),
      this.#getZeroPaddedString(now.getMinutes(), 2),
      this.#getZeroPaddedString(now.getSeconds(), 2),
    ].join('_');
  }

  #onVideoCaptureToggle(enabled) {
    const recordToggle = document.getElementById('record-video-control');
    if (enabled) {
      let recorders = [];

      const timestamp = this.#getTimestampString();

      let deviceDisplayVideoList =
        document.getElementsByClassName('device-display-video');
      for (let i = 0; i < deviceDisplayVideoList.length; i++) {
        const deviceDisplayVideo = deviceDisplayVideoList[i];

        const recorder = new MediaRecorder(deviceDisplayVideo.captureStream());
        const recordedData = [];

        recorder.ondataavailable = event => recordedData.push(event.data);
        recorder.onstop = event => {
          const recording = new Blob(recordedData, { type: "video/webm" });

          const downloadLink = document.createElement('a');
          downloadLink.setAttribute('download', timestamp + '_display_' + i + '.webm');
          downloadLink.setAttribute('href', URL.createObjectURL(recording));
          downloadLink.click();
        };

        recorder.start();
        recorders.push(recorder);
      }
      this.#recording['recorders'] = recorders;

      recordToggle.style.backgroundColor = 'red';
    } else {
      for (const recorder of this.#recording['recorders']) {
        recorder.stop();
      }
      recordToggle.style.backgroundColor = '';
    }
    return Promise.resolve(enabled);
  }

  #onAudioPlaybackToggle(enabled) {
    const audioElem = document.getElementById('device-audio');
    if (enabled) {
      audioElem.play();
    } else {
      audioElem.pause();
    }
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

window.addEventListener("load", async evt => {
  try {
    setupMessages();
    let connectorModule = await import('./server_connector.js');
    let deviceId = connectorModule.deviceId();
    document.title = deviceId;
    let deviceConnection =
        await ConnectDevice(deviceId, await connectorModule.createConnector());
    let deviceControlApp = new DeviceControlApp(deviceConnection);
    deviceControlApp.start();
    document.getElementById('device-connection').style.display = 'block';
  } catch(err) {
    console.error('Unable to connect: ', err);
    showError(
      'No connection to the guest device. ' +
      'Please ensure the WebRTC process on the host machine is active.');
  }
  document.getElementById('loader').style.display = 'none';
});

// The formulas in this function are derived from the following facts:
// * The video element's aspect ratio (ar) is fixed.
// * CSS rotations are centered on the geometrical center of the element.
// * The aspect ratio is the tangent of the angle between the left-top to
// right-bottom diagonal (d) and the left side.
// * d = w/sin(arctan(ar)) = h/cos(arctan(ar)), with w = width and h = height.
// * After any rotation, the element's total width is the maximum size of the
// projection of the diagonals on the X axis (Y axis for height).
// Deriving the formulas is left as an exercise to the reader.
function getStyleAfterRotation(rotationDeg, ar) {
  // Convert the rotation angle to radians
  let r = Math.PI * rotationDeg / 180;

  // width <= parent_with / abs(cos(r) + sin(r)/ar)
  // and
  // width <= parent_with / abs(cos(r) - sin(r)/ar)
  let den1 = Math.abs((Math.sin(r) / ar) + Math.cos(r));
  let den2 = Math.abs((Math.sin(r) / ar) - Math.cos(r));
  let denominator = Math.max(den1, den2);
  let maxWidth = `calc(100% / ${denominator})`;

  // height <= parent_height / abs(cos(r) + sin(r)*ar)
  // and
  // height <= parent_height / abs(cos(r) - sin(r)*ar)
  den1 = Math.abs(Math.cos(r) - (Math.sin(r) * ar));
  den2 = Math.abs(Math.cos(r) + (Math.sin(r) * ar));
  denominator = Math.max(den1, den2);
  let maxHeight = `calc(100% / ${denominator})`;

  // rotated_left >= left * (abs(cos(r)+sin(r)/ar)-1)/2
  // and
  // rotated_left >= left * (abs(cos(r)-sin(r)/ar)-1)/2
  let tmp1 = Math.max(
      Math.abs(Math.cos(r) + (Math.sin(r) / ar)),
      Math.abs(Math.cos(r) - (Math.sin(r) / ar)));
  let leftFactor = (tmp1 - 1) / 2;
  // rotated_top >= top * (abs(cos(r)+sin(r)*ar)-1)/2
  // and
  // rotated_top >= top * (abs(cos(r)-sin(r)*ar)-1)/2
  let tmp2 = Math.max(
      Math.abs(Math.cos(r) - (Math.sin(r) * ar)),
      Math.abs(Math.cos(r) + (Math.sin(r) * ar)));
  let rightFactor = (tmp2 - 1) / 2;

  // CSS rotations are in the opposite direction as Android screen rotations
  rotationDeg = -rotationDeg;

  let transform = `translate(calc(100% * ${leftFactor}), calc(100% * ${
      rightFactor})) rotate(${rotationDeg}deg)`;

  return {transform, maxWidth, maxHeight};
}
