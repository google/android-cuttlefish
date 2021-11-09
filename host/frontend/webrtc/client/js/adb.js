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

let adb_ws;

let utf8Encoder = new TextEncoder();
let utf8Decoder = new TextDecoder();

const A_CNXN = 0x4e584e43;
const A_OPEN = 0x4e45504f;
const A_WRTE = 0x45545257;
const A_OKAY = 0x59414b4f;

const kLocalChannelId = 666;

let array = new Uint8Array();

function setU32LE(array, offset, x) {
  array[offset] = x & 0xff;
  array[offset + 1] = (x >> 8) & 0xff;
  array[offset + 2] = (x >> 16) & 0xff;
  array[offset + 3] = x >> 24;
}

function getU32LE(array, offset) {
  let x = array[offset] | (array[offset + 1] << 8) | (array[offset + 2] << 16) |
      (array[offset + 3] << 24);

  return x >>> 0;  // convert signed to unsigned if necessary.
}

function computeChecksum(array) {
  let sum = 0;
  let i;
  for (i = 0; i < array.length; ++i) {
    sum = ((sum + array[i]) & 0xffffffff) >>> 0;
  }

  return sum;
}

function createAdbMessage(command, arg0, arg1, payload) {
  let arrayBuffer = new ArrayBuffer(24 + payload.length);
  let array = new Uint8Array(arrayBuffer);
  setU32LE(array, 0, command);
  setU32LE(array, 4, arg0);
  setU32LE(array, 8, arg1);
  setU32LE(array, 12, payload.length);
  setU32LE(array, 16, computeChecksum(payload));
  setU32LE(array, 20, command ^ 0xffffffff);
  array.set(payload, 24);

  return arrayBuffer;
}

function adbOpenConnection() {
  let systemIdentity = utf8Encoder.encode('Cray_II:1234:whatever');

  let arrayBuffer =
      createAdbMessage(A_CNXN, 0x1000000, 256 * 1024, systemIdentity);

  adb_ws.send(arrayBuffer);
}

function adbShell(command) {
  let destination = utf8Encoder.encode('shell:' + command);

  let arrayBuffer = createAdbMessage(A_OPEN, kLocalChannelId, 0, destination);
  adb_ws.send(arrayBuffer);
  awaitConnection();
}

function adbSendOkay(remoteId) {
  let payload = new Uint8Array(0);

  let arrayBuffer =
      createAdbMessage(A_OKAY, kLocalChannelId, remoteId, payload);

  adb_ws.send(arrayBuffer);
}

function JoinArrays(arr1, arr2) {
  let arr = new Uint8Array(arr1.length + arr2.length);
  arr.set(arr1, 0);
  arr.set(arr2, arr1.length);
  return arr;
}

// Simple lifecycle management that executes callbacks based on connection
// state.
//
// Any attempt to initiate a command (e.g. creating a connection, sending a
// message) (re)starts a timer. Any response back from any command stops that
// timer.
const timeoutMs = 3000;
let connectedCb;
let disconnectedCb;
let disconnectedTimeout;
function awaitConnection() {
  clearTimeout(disconnectedTimeout);
  if (disconnectedCb) {
    disconnectedTimeout = setTimeout(disconnectedCb, timeoutMs);
  }
}
function connected() {
  if (disconnectedTimeout) {
    clearTimeout(disconnectedTimeout);
  }
  if (connectedCb) {
    connectedCb();
  }
}

function adbOnMessage(arrayBuffer) {
  // console.debug("adb_ws: onmessage (" + arrayBuffer.byteLength + " bytes)");
  array = JoinArrays(array, new Uint8Array(arrayBuffer));

  while (array.length > 0) {
    if (array.length < 24) {
      // Incomplete package, must wait for more data.
      return;
    }

    let command = getU32LE(array, 0);
    let magic = getU32LE(array, 20);

    if (command != ((magic ^ 0xffffffff) >>> 0)) {
      console.error('adb message command vs magic failed.');
      console.error('command = ' + command + ', magic = ' + magic);
      return;
    }

    let payloadLength = getU32LE(array, 12);

    if (array.length < 24 + payloadLength) {
      // Incomplete package, must wait for more data.
      return;
    }

    let payloadChecksum = getU32LE(array, 16);
    let checksum = computeChecksum(array.slice(24));

    if (payloadChecksum != checksum) {
      console.error('adb message checksum mismatch.');
      // This can happen if a shell command executes while another
      // channel is receiving data.
    }

    switch (command) {
      case A_CNXN: {
        console.info('WebRTC adb connected.');
        connected();
        break;
      }

      case A_OKAY: {
        let remoteId = getU32LE(array, 4);
        console.debug('WebRTC adb channel created w/ remoteId ' + remoteId);
        connected();
        break;
      }

      case A_WRTE: {
        let remoteId = getU32LE(array, 4);
        adbSendOkay(remoteId);
        break;
      }
    }
    array = array.subarray(24 + payloadLength, array.length);
  }
}

function init_adb(devConn, ccb = connectedCb, dcb = disconnectedCb) {
  if (adb_ws) return;

  adb_ws = {
    send: function(buffer) {
      devConn.sendAdbMessage(buffer);
    }
  };
  connectedCb = ccb;
  disconnectedCb = dcb;
  awaitConnection();

  devConn.onAdbMessage(msg => adbOnMessage(msg));

  adbOpenConnection();
}
