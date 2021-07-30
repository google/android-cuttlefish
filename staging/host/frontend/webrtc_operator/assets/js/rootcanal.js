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

'use strict';

function rootCanalCalculateMessageSize(name, args) {
  let result = 0;

  result += 1 + name.length;  // length of name + it's data
  result += 1;                // count of args

  for (let i = 0; i < args.length; i++) {
    result += 1;               // length of args[i]
    result += args[i].length;  // data of args[i]
  }

  return result;
}

function rootCanalAddU8(array, pos, val) {
  array[pos] = val & 0xff;

  return pos + 1;
}

function rootCanalAddPayload(array, pos, payload) {
  array.set(payload, pos);

  return pos + payload.length;
}

function rootCanalAddString(array, pos, val) {
  let curPos = pos;

  curPos = rootCanalAddU8(array, curPos, val.length);

  return rootCanalAddPayload(array, curPos, utf8Encoder.encode(val));
}

function createRootcanalMessage(command, args) {
  let messageSize = rootCanalCalculateMessageSize(command, args);
  let arrayBuffer = new ArrayBuffer(messageSize);
  let array = new Uint8Array(arrayBuffer);
  let pos = 0;

  pos = rootCanalAddString(array, pos, command);
  pos = rootCanalAddU8(array, pos, args.length);

  for (let i = 0; i < args.length; i++) {
    pos = rootCanalAddString(array, pos, args[i]);
  }

  return array;
}

function decodeRootcanalMessage(array) {
  let size = array[0];
  let message = array.slice(1);

  return utf8Decoder.decode(message);
}
