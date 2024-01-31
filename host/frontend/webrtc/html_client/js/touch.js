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

function trackPointerEvents(touchInputElement, dc, scaleCoordinates) {
  let activePointers = new Set();

  function onPointerDown(e) {
    // Can't prevent event default behavior to allow the element gain focus
    // when touched and start capturing keyboard input in the parent.
    activePointers.add(e.pointerId);
    sendEventUpdate(dc, e.target, [{x: e.offsetX, y: e.offsetY, id: e.pointerId}], scaleCoordinates, true);
  }

  function onPointerUp(e) {
    // Can't prevent event default behavior to allow the element gain focus
    // when touched and start capturing keyboard input in the parent.
    const wasDown = activePointers.delete(e.pointerId);
    if (!wasDown) {
      return;
    }
    sendEventUpdate(dc, e.target, [{x: e.offsetX, y: e.offsetY, id: e.pointerId}], scaleCoordinates, false);
  }

  function onPointerMove(e) {
    // Can't prevent event default behavior to allow the element gain focus
    // when touched and start capturing keyboard input in the parent.
    if (!activePointers.has(e.pointerId)) {
      // This is just a mouse move, not a drag
      return;
    }
    sendEventUpdate(dc, e.target, [{x: e.offsetX, y: e.offsetY, id: e.pointerId}], scaleCoordinates, true);
  }

  touchInputElement.addEventListener('pointerdown', onPointerDown);
  touchInputElement.addEventListener('pointermove', onPointerMove);
  touchInputElement.addEventListener('pointerup', onPointerUp);
  touchInputElement.addEventListener('pointerleave', onPointerUp);
  touchInputElement.addEventListener('pointercancel', onPointerUp);
}

function scaleDisplayCoordinates(deviceDisplayElement, x, y) {
  // Before the first video frame arrives there is no way to know width and
  // height of the device's screen, so turn every click into a click at 0x0.
  // A click at that position is not more dangerous than anywhere else since
  // the user is clicking blind anyways.
  const actualWidth = deviceDisplayElement.videoWidth ? deviceDisplayElement.videoWidth : 1;
  const elementWidth = deviceDisplayElement.offsetWidth ? deviceDisplayElement.offsetWidth : 1;
  const scaling = actualWidth / elementWidth;
  return [Math.trunc(x * scaling), Math.trunc(y * scaling)];
}

function makeScaleTouchpadCoordinates(touchpad) {
  return (touchpadElement, x, y)  => {
    const elementWidth = touchpadElement.offsetWidth ? touchpadElement.offsetWidth : 1;
    const scaling = touchpad.x_res / elementWidth;
    return [Math.trunc(x * scaling), Math.trunc(y * scaling)];
  };
}

function sendEventUpdate(dc, touchInputElement, evs /*[{x, y, id}]*/, scaleCoordinates, down) {
  if (evs.length == 0) {
    return;
  }

  const device_label = touchInputElement.id;
  let xArr = [];
  let yArr = [];
  let idArr = [];

  for (const e of evs) {
      const [x_scaled, y_scaled] = scaleCoordinates(touchInputElement, e.x, e.y);
      xArr.push(x_scaled);
      yArr.push(y_scaled);
      idArr.push(e.id);
  }

  // NOTE: Rotation is handled automatically because the CSS rotation through
  // transforms also rotates the coordinates of events on the object.
  dc.sendMultiTouch(
      {idArr, xArr, yArr, down: down, device_label});
}

