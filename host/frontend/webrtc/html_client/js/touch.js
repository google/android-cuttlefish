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

function trackPointerEvents(videoElement, dc) {
  let activePointers = new Set();

  function onPointerDown(e) {
    // Can't prevent event default behavior to allow the element gain focus
    // when touched and start capturing keyboard input in the parent.
    activePointers.add(e.pointerId);
    sendEventUpdate(dc, e.target, [{x: e.offsetX, y: e.offsetY, id: e.pointerId}], true);
  }

  function onPointerUp(e) {
    // Can't prevent event default behavior to allow the element gain focus
    // when touched and start capturing keyboard input in the parent.
    const wasDown = activePointers.delete(e.pointerId);
    if (!wasDown) {
      return;
    }
    sendEventUpdate(dc, e.target, [{x: e.offsetX, y: e.offsetY, id: e.pointerId}], false);
  }

  function onPointerMove(e) {
    // Can't prevent event default behavior to allow the element gain focus
    // when touched and start capturing keyboard input in the parent.
    if (!activePointers.has(e.pointerId)) {
      // This is just a mouse move, not a drag
      return;
    }
    sendEventUpdate(dc, e.target, [{x: e.offsetX, y: e.offsetY, id: e.pointerId}], true);
  }

  videoElement.addEventListener('pointerdown', onPointerDown);
  videoElement.addEventListener('pointermove', onPointerMove);
  videoElement.addEventListener('pointerup', onPointerUp);
  videoElement.addEventListener('pointerleave', onPointerUp);
  videoElement.addEventListener('pointercancel', onPointerUp);
}

function sendEventUpdate(dc, deviceDisplay, evs /*[{x, y, id}]*/, down) {
  if (evs.length == 0) {
    return;
  }

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

  for (const e of evs) {
      xArr.push(e.x);
      yArr.push(e.y);
      idArr.push(e.id);
  }

  for (const i in xArr) {
    xArr[i] = Math.trunc(xArr[i] * scaling);
    yArr[i] = Math.trunc(yArr[i] * scaling);
  }

  // NOTE: Rotation is handled automatically because the CSS rotation through
  // transforms also rotates the coordinates of events on the object.

  const device_label = deviceDisplay.id;

  dc.sendMultiTouch(
      {idArr, xArr, yArr, down: down, device_label});
}

