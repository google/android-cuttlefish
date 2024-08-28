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

// Set the theme as soon as possible.
const params = new URLSearchParams(location.search);
let theme = params.get('theme');
if (theme === 'light') {
  document.querySelector('body').classList.add('light-theme');
} else if (theme === 'dark') {
  document.querySelector('body').classList.add('dark-theme');
}

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

// These classes provide the same interface as those from the server_connector,
// but can't inherit from them because older versions of server_connector.js
// don't provide them.
// These classes are only meant to avoid having to check for null every time.
class EmptyDeviceDisplaysMessage {
  addDisplay(display_id, width, height) {}
  send() {}
}

class EmptyParentController {
  createDeviceDisplaysMessage(rotation) {
    return new EmptyDeviceDisplaysMessage();
  }
}

class DeviceControlApp {
  #deviceConnection = {};
  #parentController = null;
  #currentRotation = 0;
  #currentScreenStyles = {};
  #displayDescriptions = [];
  #recording = {};
  #phys = {};
  #deviceCount = 0;
  #micActive = false;
  #adbConnected = false;

  constructor(deviceConnection, parentController) {
    this.#deviceConnection = deviceConnection;
    this.#parentController = parentController;
  }

  start() {
    console.debug('Device description: ', this.#deviceConnection.description);
    this.#deviceConnection.onControlMessage(msg => this.#onControlMessage(msg));
    this.#deviceConnection.onLightsMessage(msg => this.#onLightsMessage(msg));
    this.#deviceConnection.onSensorsMessage(msg => this.#onSensorsMessage(msg));
    createToggleControl(
        document.getElementById('camera_off_btn'),
        enabled => this.#onCameraCaptureToggle(enabled));
    // disable the camera button if we are not using VSOCK camera
    if (!this.#deviceConnection.description.hardware.camera_passthrough) {
      document.getElementById('camera_off_btn').style.display = "none";
    }
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

    // Enable non-ADB buttons, these buttons use data channels to communicate
    // with the host, so they're ready to go as soon as the webrtc connection is
    // established.
    this.#getControlPanelButtons()
        .filter(b => !b.dataset.adb)
        .forEach(b => b.disabled = false);

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
        'rotation-modal-button', 'rotation-modal',
        'rotation-modal-close');
    createModalButton(
      'touchpad-modal-button', 'touchpad-modal',
      'touchpad-modal-close');
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

    createModalButton('location-modal-button', 'location-prompt-modal',
        'location-prompt-modal-close');
    createModalButton(
        'location-set-wizard', 'location-set-modal', 'location-set-modal-close',
        'location-prompt-modal');

    createModalButton(
        'locations-import-wizard', 'locations-import-modal', 'locations-import-modal-close',
        'location-prompt-modal');
    createModalButton(
        'location-set-cancel', 'location-prompt-modal', 'location-set-modal-close',
        'location-set-modal');
    positionModal('rotation-modal-button', 'rotation-modal');
    positionModal('device-details-button', 'bluetooth-modal');
    positionModal('device-details-button', 'bluetooth-prompt');
    positionModal('device-details-button', 'bluetooth-wizard');
    positionModal('device-details-button', 'bluetooth-wizard-confirm');
    positionModal('device-details-button', 'bluetooth-list');
    positionModal('device-details-button', 'bluetooth-console');

    positionModal('device-details-button', 'location-modal');
    positionModal('device-details-button', 'location-prompt-modal');
    positionModal('device-details-button', 'location-set-modal');
    positionModal('device-details-button', 'locations-import-modal');

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

    createButtonListener('locations-send-btn', null, this.#deviceConnection,
      evt => this.#onImportLocationsFile(this.#deviceConnection,evt));

    createButtonListener('location-set-confirm', null, this.#deviceConnection,
      evt => this.#onSendLocation(this.#deviceConnection, evt));

    createButtonListener('left-position-button', null, this.#deviceConnection,
      () => this.#setOrientation(-90));
    createButtonListener('upright-position-button', null, this.#deviceConnection,
      () => this.#setOrientation(0));

    createButtonListener('right-position-button', null, this.#deviceConnection,
      () => this.#setOrientation(90));

    createButtonListener('upside-position-button', null, this.#deviceConnection,
      () => this.#setOrientation(-180));

    createSliderListener('rotation-slider', () => this.#onMotionChanged(this.#deviceConnection));

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
    this.#updateDeviceDisplays();
    this.#deviceConnection.onStreamChange(stream => this.#onStreamChange(stream));

    // Set up audio
    const deviceAudio = document.getElementById('device-audio');
    for (const audio_desc of this.#deviceConnection.description.audio_streams) {
      let stream_id = audio_desc.stream_id;
      this.#deviceConnection.onStream(stream_id)
          .then(stream => {
            deviceAudio.srcObject = stream;
            deviceAudio.play();
          })
          .catch(e => console.error('Unable to get audio stream: ', e));
    }

    // Set up keyboard and wheel capture
    this.#startKeyboardCapture();
    this.#startWheelCapture();

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
      console.debug("deviceCount= " +deviceCount);
      console.debug("decoded= " +decoded);
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

    this.#deviceConnection.onLocationMessage(msg => {
      console.debug("onLocationMessage = " +msg);
    });
  }

  #onStreamChange(stream) {
    let stream_id = stream.id;
    if (stream_id.startsWith('display_')) {
      this.#updateDeviceDisplays();
    }
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

  #onSendLocation(deviceConnection, evt) {

    let longitude = document.getElementById('location-set-longitude').value;
    let latitude = document.getElementById('location-set-latitude').value;
    let altitude = document.getElementById('location-set-altitude').value;
    if (longitude == null || longitude == '' || latitude == null  || latitude == ''||
        altitude == null  || altitude == '') {
      return;
    }
    let location_msg = longitude + "," +latitude + "," + altitude;
    deviceConnection.sendLocationMessage(location_msg);
  }

  async #onSensorsMessage(message) {
    var decoder = new TextDecoder("utf-8");
    message = decoder.decode(message.data);

    // Get sensor values from message.
    var sensor_vals = message.split(" ");
    sensor_vals = sensor_vals.map((val) => parseFloat(val).toFixed(3));

    const acc_val = document.getElementById('accelerometer-value');
    const mgn_val = document.getElementById('magnetometer-value');
    const gyro_val = document.getElementById('gyroscope-value');
    const xyz_val = document.getElementsByClassName('rotation-slider-value');
    const xyz_range = document.getElementsByClassName('rotation-slider-range');

    // TODO: move to webrtc backend.
    // Inject sensors with new values.
    adbShell(`/vendor/bin/cuttlefish_sensor_injection motion ${sensor_vals[3]} ${sensor_vals[4]} ${sensor_vals[5]} ${sensor_vals[6]} ${sensor_vals[7]} ${sensor_vals[8]} ${sensor_vals[9]} ${sensor_vals[10]} ${sensor_vals[11]}`);

    // Display new sensor values after injection.
    acc_val.textContent = `${sensor_vals[3]} ${sensor_vals[4]} ${sensor_vals[5]}`;
    mgn_val.textContent = `${sensor_vals[6]} ${sensor_vals[7]} ${sensor_vals[8]}`;
    gyro_val.textContent = `${sensor_vals[9]} ${sensor_vals[10]} ${sensor_vals[11]}`;

    // Update xyz sliders with backend values.
    // This is needed for preserving device's state when display is turned on
    // and off, and for having the same state for multiple clients.
    for(let i = 0; i < 3; i++) {
      xyz_val[i].textContent = sensor_vals[i];
      xyz_range[i].value = sensor_vals[i];
    }
  }

  // Send new rotation angles for sensor values' processing.
  #onMotionChanged(deviceConnection = this.#deviceConnection) {
    let values = document.getElementsByClassName('rotation-slider-value');
    let xyz = [];
    for (var i = 0; i < values.length; i++) {
      xyz[i] = values[i].innerHTML;
    }
    deviceConnection.sendSensorsMessage(`${xyz[0]} ${xyz[1]} ${xyz[2]}`);
  }

  // Gradually rotate to a fixed orientation.
  #setOrientation(z) {
    const sliders = document.getElementsByClassName('rotation-slider-range');
    const values = document.getElementsByClassName('rotation-slider-value');
    if (sliders.length != values.length && sliders.length != 3) {
      return;
    }
    // Set XY axes to 0 (upright position).
    sliders[0].value = '0';
    values[0].textContent = '0';
    sliders[1].value = '0';
    values[1].textContent = '0';

    // Gradually transition z axis to target angle.
    let current_z = parseFloat(sliders[2].value);
    const step = ((z > current_z) ? 0.5 : -0.5);
    let move = setInterval(() => {
      if (Math.abs(z - current_z) >= 0.5) {
        current_z += step;
      }
      else {
        current_z = z;
      }
      sliders[2].value = current_z;
      values[2].textContent = `${current_z}`;
      this.#onMotionChanged();
      if (current_z == z) {
        this.#onMotionChanged();
        clearInterval(move);
      }
    }, 5);
  }

  #onImportLocationsFile(deviceConnection, evt) {

    function onLoad_send_kml_data(xml) {
      deviceConnection.sendKmlLocationsMessage(xml);
    }

    function onLoad_send_gpx_data(xml) {
      deviceConnection.sendGpxLocationsMessage(xml);
    }

    let file_selector=document.getElementById("locations_select_file");

    if (!file_selector.files) {
        alert("input parameter is not of file type");
        return;
    }

    if (!file_selector.files[0]) {
        alert("Please select a file ");
        return;
    }

    var filename= file_selector.files[0];
    if (filename.type.match('\gpx')) {
      console.debug("import Gpx locations handling");
      loadFile(onLoad_send_gpx_data);
    } else if(filename.type.match('\kml')){
      console.debug("import Kml locations handling");
      loadFile(onLoad_send_kml_data);
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

  #getControlPanelButtons() {
    return [
      ...document.querySelectorAll('#control-panel-default-buttons button'),
      ...document.querySelectorAll('#control-panel-custom-buttons button'),
    ];
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
      const width = v.videoWidth;
      const height = v.videoHeight;
      if (!width  || !height) {
        console.error('Stream dimensions not yet available?', v);
        return;
      }

      const aspectRatio = width / height;

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

    // #currentRotation is device's physical rotation and currently used to
    // determine display's rotation. It would be obtained from device's
    // accelerometer sensor.
    let deviceDisplaysMessage =
        this.#parentController.createDeviceDisplaysMessage(
            this.#currentRotation);

    labels.forEach(l => {
      let deviceDisplay = l.closest('.device-display');
      if (deviceDisplay == null) {
        console.error('Missing corresponding device display', l);
        return;
      }

      let deviceDisplayVideo =
          deviceDisplay.querySelector('.device-display-video');
      if (deviceDisplayVideo == null) {
        console.error('Missing corresponding device display video', l);
        return;
      }

      const DISPLAY_PREFIX = 'display_';
      let displayId = deviceDisplayVideo.id;
      if (displayId == null || !displayId.startsWith(DISPLAY_PREFIX)) {
        console.error('Unexpected device display video id', displayId);
        return;
      }
      displayId = displayId.slice(DISPLAY_PREFIX.length);

      let stream = deviceDisplayVideo.srcObject;
      if (stream == null) {
        console.error('Missing corresponding device display video stream', l);
        return;
      }

      let text = `Display ${displayId} `;

      let streamVideoTracks = stream.getVideoTracks();
      if (streamVideoTracks.length > 0) {
        let streamSettings = stream.getVideoTracks()[0].getSettings();
        // Width and height may not be available immediately after the track is
        // added but before frames arrive.
        if ('width' in streamSettings && 'height' in streamSettings) {
          let streamWidth = streamSettings.width;
          let streamHeight = streamSettings.height;

          deviceDisplaysMessage.addDisplay(
              displayId, streamWidth, streamHeight);

          text += `${streamWidth}x${streamHeight}`;
        }
      }

      if (this.#currentRotation != 0) {
        text += ` (Rotated ${this.#currentRotation}deg)`
      }

      l.textContent = text;
    });

    deviceDisplaysMessage.send();
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
      this.#deviceConnection.expectStreamChange();
      this.#updateDisplayVisibility(metadata.display, metadata.mode);
    }
  }

  #onLightsMessage(message) {
    let message_data = JSON.parse(message.data);
    // TODO(286106270): Add an UI component for this
    console.debug('Lights message received: ', message_data)
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
  #updateDeviceDisplays() {
    let anyDisplayLoaded = false;
    const deviceDisplays = document.getElementById('device-displays');

    const MAX_DISPLAYS = 16;
    for (let i = 0; i < MAX_DISPLAYS; i++) {
      const stream_id = 'display_' + i.toString();
      const stream = this.#deviceConnection.getStream(stream_id);

      let deviceDisplayVideo = document.querySelector('#' + stream_id);
      const deviceDisplayIsPresent = deviceDisplayVideo != null;
      const deviceDisplayStreamIsActive = stream != null && stream.active;
      if (deviceDisplayStreamIsActive == deviceDisplayIsPresent) {
          continue;
      }

      if (deviceDisplayStreamIsActive) {
        console.debug('Adding display', i);

        let displayFragment =
            document.querySelector('#display-template').content.cloneNode(true);

        let deviceDisplayInfo =
            displayFragment.querySelector('.device-display-info');
        deviceDisplayInfo.id = stream_id + '_info';

        deviceDisplayVideo = displayFragment.querySelector('video');
        deviceDisplayVideo.id = stream_id;
        deviceDisplayVideo.srcObject = stream;
        deviceDisplayVideo.addEventListener('loadeddata', (evt) => {
          if (!anyDisplayLoaded) {
            anyDisplayLoaded = true;
            this.#onDeviceDisplayLoaded();
          }
        });
        deviceDisplayVideo.addEventListener('loadedmetadata', (evt) => {
          this.#updateDeviceDisplaysInfo();
        });

        this.#addMouseTracking(deviceDisplayVideo, scaleDisplayCoordinates);

        deviceDisplays.appendChild(displayFragment);

        // Confusingly, events for adding tracks occur on the peer connection
        // but events for removing tracks occur on the stream.
        stream.addEventListener('removetrack', evt => {
          this.#updateDeviceDisplays();
        });

        this.#requestNewFrameForDisplay(i);
      } else {
        console.debug('Removing display', i);

        let deviceDisplay = deviceDisplayVideo.closest('.device-display');
        if (deviceDisplay == null) {
          console.error('Failed to find device display for ', stream_id);
        } else {
          deviceDisplays.removeChild(deviceDisplay);
        }
      }
    }

    this.#updateDeviceDisplaysInfo();
  }

  #requestNewFrameForDisplay(display_number) {
    let message = {
      command: "display",
      refresh_display: display_number,
    };
    this.#deviceConnection.sendControlMessage(JSON.stringify(message));
    console.debug('Control message sent: ', JSON.stringify(message));
  }

  #initializeAdb() {
    init_adb(
        this.#deviceConnection, () => this.#onAdbConnected(),
        () => this.#showAdbError());
  }

  #onAdbConnected() {
    if (this.#adbConnected) {
       return;
    }
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

  #initializeTouchpads() {
    const touchpadListElem = document.getElementById("touchpad-list");
    const touchpadElementContainer = touchpadListElem.querySelector(".touchpads");
    const touchpadSelectorContainer = touchpadListElem.querySelector(".selectors");
    const touchpads = this.#deviceConnection.description.touchpads;

    let setActiveTouchpad = (tab_touchpad_id, touchpad_num) => {
      const touchPadElem = document.getElementById(tab_touchpad_id);
      const tabButtonElem = document.getElementById("touch_button_" + touchpad_num);

      touchpadElementContainer.querySelectorAll(".selected").forEach(e => e.classList.remove("selected"));
      touchpadSelectorContainer.querySelectorAll(".selected").forEach(e => e.classList.remove("selected"));

      touchPadElem.classList.add("selected");
      tabButtonElem.classList.add("selected");
    };

    for (let i = 0; i < touchpads.length; i++) {
      const touchpad = touchpads[i];

      let touchPadElem = document.createElement("div");
      touchPadElem.classList.add("touchpad");
      touchPadElem.style.aspectRatio = touchpad.x_res / touchpad.y_res;
      touchPadElem.id = touchpad.label;
      this.#addMouseTracking(touchPadElem, makeScaleTouchpadCoordinates(touchpad));
      touchpadElementContainer.appendChild(touchPadElem);

      let tabButtonElem = document.createElement("button");
      tabButtonElem.id = "touch_button_" + i;
      tabButtonElem.innerHTML = "Touchpad " + i;
      tabButtonElem.class = "touchpad-tab-button"
      tabButtonElem.onclick = () => {
        setActiveTouchpad(touchpad.label, i);
      };
      touchpadSelectorContainer.appendChild(tabButtonElem);
    }

    if (touchpads.length > 0) {
      document.getElementById("touchpad-modal-button").style.display = "block";
      setActiveTouchpad(touchpads[0].label, 0);
    }
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

    this.#initializeTouchpads();

    // Start the adb connection if it is not already started.
    this.#initializeAdb();
    // TODO(b/297361564)
    this.#onMotionChanged();
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
    if (e.cancelable) {
      // Some keyboard events cause unwanted side effects, like elements losing
      // focus, if the default behavior is not prevented.
      e.preventDefault();
    }
    this.#deviceConnection.sendKeyEvent(e.code, e.type);
  }

  #startWheelCapture() {
    const deviceArea = document.querySelector('#device-displays');
    deviceArea.addEventListener('wheel', evt => this.#onWheelEvent(evt),
                                { passive: false });
  }

  #onWheelEvent(e) {
    e.preventDefault();
    // Vertical wheel pixel events only
    if (e.deltaMode == WheelEvent.DOM_DELTA_PIXEL && e.deltaY != 0.0) {
      this.#deviceConnection.sendWheelEvent(e.deltaY);
    }
  }

  #addMouseTracking(touchInputElement, scaleCoordinates) {
    trackPointerEvents(touchInputElement, this.#deviceConnection, scaleCoordinates);
  }

  #updateDisplayVisibility(displayId, powerMode) {
    const displayVideo = document.getElementById('display_' + displayId);
    if (displayVideo == null) {
      console.error('Unknown display id: ' + displayId);
      return;
    }

    const display = displayVideo.parentElement;
    if (display == null) {
      console.error('Unknown display id: ' + displayId);
      return;
    }

    const display_number = parseInt(displayId);
    if (isNaN(display_number)) {
      console.error('Invalid display id: ' + displayId);
      return;
    }

    powerMode = powerMode.toLowerCase();
    switch (powerMode) {
      case 'on':
        display.style.visibility = 'visible';
        this.#requestNewFrameForDisplay(display_number);
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
    this.#deviceConnection.useMic(nextState,
      () => document.querySelector('#mic_btn').innerHTML = 'mic',
      () => document.querySelector('#mic_btn').innerHTML = 'mic_off');
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
    let parentController = null;
    if (connectorModule.createParentController) {
      parentController = connectorModule.createParentController();
    }
    if (!parentController) {
      parentController = new EmptyParentController();
    }
    let deviceControlApp = new DeviceControlApp(deviceConnection, parentController);
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
