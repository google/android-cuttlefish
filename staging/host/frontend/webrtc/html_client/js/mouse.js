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

function enableMouseButton(dc) {
  function onMouseKey(evt) {
    if (evt.cancelable) {
      // Some keyboard events cause unwanted side effects, like elements losing
      // focus, if the default behavior is not prevented.
      evt.preventDefault();
    }
    dc.sendKeyEvent(evt.code, evt.type);
  }
  function onMouseWheel(evt) {
    evt.preventDefault();
    // Vertical wheel pixel events only
    if (evt.deltaMode == WheelEvent.DOM_DELTA_PIXEL && evt.deltaY != 0.0) {
      dc.sendMouseWheelEvent(evt.deltaY);
    }
  }
  let button = document.getElementById("mouse_btn");
  button.style.display = "";
  button.disabled = false;
  trackMouseEvents(dc, button);
  button.addEventListener('keydown', onMouseKey);
  button.addEventListener('keyup', onMouseKey);
  button.addEventListener('wheel', onMouseWheel,
                                { passive: false });
  return button;
}

