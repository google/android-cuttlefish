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
    // boolean parameter indicating whether the toggle is in ON position.
    OnClick: cb => input.onchange = e => cb(e.target.checked),
  };
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
