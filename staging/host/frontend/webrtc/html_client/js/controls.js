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

// Creates a "toggle control". The onToggleCb callback is called every time the
// control changes state with the new toggle position (true for ON) and is
// expected to return a promise of the new toggle position which can resolve to
// the opposite position of the one received if there was error.
function createToggleControl(elm, onToggleCb, initialState = false) {
  elm.classList.add('toggle-control');
  let offClass = 'toggle-off';
  let onClass = 'toggle-on';
  let state = !!initialState;
  let toggle = {
    // Sets the state of the toggle control. This only affects the
    // visible state of the control in the UI, it doesn't affect the
    // state of the underlying resources. It's most useful to make
    // changes of said resources visible to the user.
    Set: enabled => {
      state = enabled;
      if (enabled) {
        elm.classList.remove(offClass);
        elm.classList.add(onClass);
      } else {
        elm.classList.add(offClass);
        elm.classList.remove(onClass);
      }
    }
  };
  toggle.Set(initialState);
  addMouseListeners(elm, e => {
    if (e.type != 'mousedown') {
      return;
    }
    // Enable it if it's currently disabled
    let enableNow = !state;
    let nextPr = onToggleCb(enableNow);
    if (nextPr && 'then' in nextPr) {
      nextPr.then(enabled => toggle.Set(enabled));
    }
  });
  return toggle;
}

function createButtonListener(button_id_class, func,
  deviceConnection, listener) {
  let buttons = [];
  let ele = document.getElementById(button_id_class);
  if (ele != null) {
    buttons.push(ele);
  } else {
    buttons = document.getElementsByClassName(button_id_class);
  }
  for (var button of buttons) {
    if (func != null) {
      button.onclick = func;
    }
    button.addEventListener('mousedown', listener);
  }
}

function createInputListener(input_id, func, listener) {
  input = document.getElementById(input_id);
  if (func != null) {
    input.oninput = func;
  }
  input.addEventListener('input', listener);
}

function validateMacAddress(val) {
  var regex = /^([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})$/;
  return (regex.test(val));
}

function validateMacWrapper() {
  let type = document.getElementById('bluetooth-wizard-type').value;
  let button = document.getElementById("bluetooth-wizard-device");
  let macField = document.getElementById('bluetooth-wizard-mac');
  if (this.id == 'bluetooth-wizard-type') {
    if (type == "remote_loopback") {
      button.disabled = false;
      macField.setCustomValidity('');
      macField.disabled = true;
      macField.required = false;
      macField.placeholder = 'N/A';
      macField.value = '';
      return;
    }
  }
  macField.disabled = false;
  macField.required = true;
  macField.placeholder = 'Device MAC';
  if (validateMacAddress($(macField).val())) {
    button.disabled = false;
    macField.setCustomValidity('');
  } else {
    button.disabled = true;
    macField.setCustomValidity('MAC address invalid');
  }
}

$('[validate-mac]').bind('input', validateMacWrapper);
$('[validate-mac]').bind('select', validateMacWrapper);

function parseDevice(device) {
  let id, name, mac;
  var regex = /([0-9]+):([^@ ]*)(@(([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})))?/;
  if (regex.test(device)) {
    let regexMatches = device.match(regex);
    id = regexMatches[1];
    name = regexMatches[2];
    mac = regexMatches[4];
  }
  if (mac === undefined) {
    mac = "";
  }
  return [id, name, mac];
}

function btUpdateAdded(devices) {
  let deviceArr = devices.split('\r\n');
  let [id, name, mac] = parseDevice(deviceArr[0]);
  if (name) {
    let div = document.getElementById('bluetooth-wizard-confirm').getElementsByClassName('bluetooth-text')[1];
    div.innerHTML = "";
    div.innerHTML += "<p>Name: <b>" + id + "</b></p>";
    div.innerHTML += "<p>Type: <b>" + name + "</b></p>";
    div.innerHTML += "<p>MAC Addr: <b>" + mac + "</b></p>";
    return true;
  }
  return false;
}

function parsePhy(phy) {
  let id = phy.substring(0, phy.indexOf(":"));
  phy = phy.substring(phy.indexOf(":") + 1);
  let name = phy.substring(0, phy.indexOf(":"));
  let devices = phy.substring(phy.indexOf(":") + 1);
  return [id, name, devices];
}

function btParsePhys(phys) {
  if (phys.indexOf("Phys:") < 0) {
    return null;
  }
  let phyDict = {};
  phys = phys.split('Phys:')[1];
  let phyArr = phys.split('\r\n');
  for (var phy of phyArr.slice(1)) {
    phy = phy.trim();
    if (phy.length == 0 || phy.indexOf("deleted") >= 0) {
      continue;
    }
    let [id, name, devices] = parsePhy(phy);
    phyDict[name] = id;
  }
  return phyDict;
}

function btUpdateDeviceList(devices) {
  let deviceArr = devices.split('\r\n');
  if (deviceArr[0].indexOf("Devices:") >= 0) {
    let div = document.getElementById('bluetooth-list').getElementsByClassName('bluetooth-text')[0];
    div.innerHTML = "";
    let count = 0;
    for (var device of deviceArr.slice(1)) {
      if (device.indexOf("Phys:") >= 0) {
        break;
      }
      count++;
      if (device.indexOf("deleted") >= 0) {
        continue;
      }
      let [id, name, mac] = parseDevice(device);
      let innerDiv = '<div><button title="Delete" data-device-id="'
      innerDiv += id;
      innerDiv += '" class="bluetooth-list-trash material-icons">delete</button>';
      innerDiv += name;
      if (mac) {
        innerDiv += " | "
        innerDiv += mac;
      }
      innerDiv += '</div>';
      div.innerHTML += innerDiv;
    }
    return count;
  }
  return -1;
}

function addMouseListeners(button, listener) {
  // Capture mousedown/up/out commands instead of click to enable
  // hold detection. mouseout is used to catch if the user moves the
  // mouse outside the button while holding down.
  button.addEventListener('mousedown', listener);
  button.addEventListener('mouseup', listener);
  button.addEventListener('mouseout', listener);
}

function createControlPanelButton(
    title, icon_name, listener, parent_id = 'control-panel-default-buttons') {
  let button = document.createElement('button');
  document.getElementById(parent_id).appendChild(button);
  button.title = title;
  button.disabled = true;
  addMouseListeners(button, listener);
  // Set the button image using Material Design icons.
  // See http://google.github.io/material-design-icons
  // and https://material.io/resources/icons
  button.classList.add('material-icons');
  button.innerHTML = icon_name;
  return button;
}

function positionModal(button_id, modal_id) {
  const modalButton = document.getElementById(button_id);
  const modalDiv = document.getElementById(modal_id);

  // Position the modal to the right of the show modal button.
  modalDiv.style.top = modalButton.offsetTop;
  modalDiv.style.left = modalButton.offsetWidth + 30;
}

function createModalButton(button_id, modal_id, close_id, hide_id) {
  const modalButton = document.getElementById(button_id);
  const modalDiv = document.getElementById(modal_id);
  const modalHeader = modalDiv.querySelector('.modal-header');
  const modalClose = document.getElementById(close_id);
  const modalDivHide = document.getElementById(hide_id);

  positionModal(button_id, modal_id);

  function showHideModal(show) {
    if (show) {
      modalButton.classList.add('modal-button-opened')
      modalDiv.style.display = 'block';
    } else {
      modalButton.classList.remove('modal-button-opened')
      modalDiv.style.display = 'none';
    }
    if (modalDivHide != null) {
      modalDivHide.style.display = 'none';
    }
  }
  // Allow the show modal button to toggle the modal,
  modalButton.addEventListener(
      'click', evt => showHideModal(modalDiv.style.display != 'block'));
  // but the close button always closes.
  modalClose.addEventListener('click', evt => showHideModal(false));

  // Allow the modal to be dragged by the header.
  let modalOffsets = {
    midDrag: false,
    mouseDownOffsetX: null,
    mouseDownOffsetY: null,
  };
  modalHeader.addEventListener('mousedown', evt => {
    modalOffsets.midDrag = true;
    // Store the offset of the mouse location from the
    // modal's current location.
    modalOffsets.mouseDownOffsetX = parseInt(modalDiv.style.left) - evt.clientX;
    modalOffsets.mouseDownOffsetY = parseInt(modalDiv.style.top) - evt.clientY;
  });
  modalHeader.addEventListener('mousemove', evt => {
    let offsets = modalOffsets;
    if (offsets.midDrag) {
      // Move the modal to the mouse location plus the
      // offset calculated on the initial mouse-down.
      modalDiv.style.left = evt.clientX + offsets.mouseDownOffsetX;
      modalDiv.style.top = evt.clientY + offsets.mouseDownOffsetY;
    }
  });
  document.addEventListener('mouseup', evt => {
    modalOffsets.midDrag = false;
  });
}

function cmdConsole(consoleViewName, consoleInputName) {
  let consoleView = document.getElementById(consoleViewName);

  let addString =
      function(str) {
    consoleView.value += str;
    consoleView.scrollTop = consoleView.scrollHeight;
  }

  let addLine =
      function(line) {
    addString(line + '\r\n');
  }

  let commandCallbacks = [];

  let addCommandListener =
      function(f) {
    commandCallbacks.push(f);
  }

  let onCommand =
      function(cmd) {
    cmd = cmd.trim();

    if (cmd.length == 0) return;

    commandCallbacks.forEach(f => {
      f(cmd);
    })
  }

  addCommandListener(cmd => addLine('>> ' + cmd));

  let consoleInput = document.getElementById(consoleInputName);

  consoleInput.addEventListener('keydown', e => {
    if ((e.key && e.key == 'Enter') || e.keyCode == 13) {
      let command = e.target.value;

      e.target.value = '';

      onCommand(command);
    }
  });

  return {
    consoleView: consoleView,
    consoleInput: consoleInput,
    addLine: addLine,
    addString: addString,
    addCommandListener: addCommandListener,
  };
}
