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

function createToggleControl(elm, iconName, onChangeCb) {
  let icon = document.createElement('span');
  icon.classList.add('toggle-control-icon');
  icon.classList.add('material-icons-outlined');
  if (iconName) {
    icon.appendChild(document.createTextNode(iconName));
  }
  elm.appendChild(icon);
  let toggle = document.createElement('label');
  toggle.classList.add('toggle-control-switch');
  let input = document.createElement('input');
  input.type = 'checkbox';
  toggle.appendChild(input);
  let slider = document.createElement('span');
  slider.classList.add('toggle-control-slider');
  toggle.appendChild(slider);
  elm.classList.add('toggle-control');
  elm.appendChild(toggle);
  return {
    // A callback can later be associated with the toggle element by calling
    // .OnClick(onChangeCb) on the returned object. The callback should accept a
    // boolean parameter indicating whether the toggle is in ON position and
    // return a promise of the new position.
    OnClick: cb => input.onchange =
        e => {
          let nextPr = cb(e.target.checked);
          if (nextPr && 'then' in nextPr) {
            nextPr.then(checked => {
              e.target.checked = !!checked;
            });
          }
        },
  };
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

$('[validate-mac]').bind('input', function() {
    var button = document.getElementById("bluetooth-wizard-device");
    if (validateMacAddress($(this).val())) {
      button.disabled = false;
      this.setCustomValidity('');
    } else {
      button.disabled = true;
      this.setCustomValidity('MAC address invalid');
    }
});

function parseDevice(device) {
  let id = device.substring(0, device.indexOf(":"));
  device = device.substring(device.indexOf(":")+1);
  let name = device.substring(0, device.indexOf("@"));
  let mac = device.substring(device.indexOf("@")+1);
  return [id, name, mac];
}

function btUpdateAdded(devices) {
  let deviceArr = devices.split('\r\n');
  if (deviceArr[0].indexOf("Devices:") >= 0) {
    return false;
  }
  if (deviceArr[0].indexOf(":") >= 0 && deviceArr[0].indexOf("@") >= 0) {
    let [id, name, mac] = parseDevice(deviceArr[0]);
    let div = document.getElementById('bluetooth-wizard-confirm').getElementsByClassName('bluetooth-text')[1];
    div.innerHTML = "";
    div.innerHTML += "<p>Name: <b>" + id + "</b></p>";
    div.innerHTML += "<p>Type: <b>" + name + "</b></p>";
    div.innerHTML += "<p>MAC Addr: <b>" + mac + "</b></p>";
    return true;
  }
  return false;
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

function createControlPanelButton(
    command, title, icon_name, listener,
    parent_id = 'control-panel-default-buttons') {
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
  return button;
}

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
