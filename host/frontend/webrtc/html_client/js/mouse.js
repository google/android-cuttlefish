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

function trackMouseEvents(dc, mouseElement) {
  function onMouseDown(evt) {
    if (!document.pointerLockElement) {
      mouseElement.requestPointerLock({});
      return;
    }
    dc.sendMouseButton({button: evt.button, down: true});
  }

  function onMouseUp(evt) {
    if (document.pointerLockElement) {
      dc.sendMouseButton({button: evt.button, down: false});
    }
  }

  function onMouseMove(evt) {
    if (document.pointerLockElement) {
      dc.sendMouseMove({x: evt.movementX, y: evt.movementY});
      dc.sendMouseButton({button: evt.button, down: evt.buttons > 0});
    }
  }
  mouseElement.addEventListener('mousedown', onMouseDown);
  mouseElement.addEventListener('mouseup', onMouseUp);
  mouseElement.addEventListener('mousemove', onMouseMove);
}

function enableMouseButton(dc, key_event_listener,
  wheel_event_listener) {
  let button = document.getElementById("mouse_btn");
  button.style.display = "";
  button.disabled = false;
  trackMouseEvents(dc, button);
  button.addEventListener('keydown', key_event_listener);
  button.addEventListener('keyup', key_event_listener);
  button.addEventListener('wheel', wheel_event_listener,
                                { passive: false });
  return button;
}

