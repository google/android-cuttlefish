/*
 * Copyright (C) 2024 The Android Open Source Project
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

function processButton(buttonName, keyCode, dc) {
  function onMouseDown(evt) {
    dc.sendKeyEvent(keyCode, "keydown");
  }

  function onMouseUp(evt) {
    dc.sendKeyEvent(keyCode, "keyup");
  }
  let button = document.getElementById(buttonName);
  button.addEventListener('mousedown', onMouseDown);
  button.addEventListener('mouseup', onMouseUp);
}

function processToggleButton(buttonName, keyCode, dc) {
  let toggle = false;
  function onMouseDown(evt) {
    const kPrimaryButton = 1;
    if ((evt.buttons & kPrimaryButton) == 0) {
      return;
    }
    toggle = !toggle;
    if (toggle) {
      dc.sendKeyEvent(keyCode, "keydown");
    } else {
      dc.sendKeyEvent(keyCode, "keyup");
    }
    this.classList.toggle('active');
  }

  let button = document.getElementById(buttonName);
  button.addEventListener('mousedown', onMouseDown);
}

function enableKeyboardRewriteButton(dc) {
  processToggleButton("shift-button", "ShiftLeft", dc);
  processToggleButton("ctrl-button", "CtrlLeft", dc);
  processToggleButton("alt-button", "AltLeft", dc);
  processToggleButton("super-button", "MetaLeft", dc);
  processButton("tab-button", "Tab", dc);
}
