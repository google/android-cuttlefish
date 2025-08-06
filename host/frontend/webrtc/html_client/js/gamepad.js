/*
 * Copyright (C) 2025 The Android Open Source Project
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

const ABS_CODES = [0x00, 0x01, 0x03, 0x04, 0x00, 0x00, 0x02, 0x05];

function enableGamepadButton(dc) {
  const trigger_value_range = 255;
  const joystick_value_range = 32768;

  let gamepadEnabled = false;
  let animationFrameId = null;

  let offClass = 'toggle-off';
  let onClass = 'toggle-on';

  function onMouseDown(evt) {
    gamepadEnabled = !gamepadEnabled;
    let button = evt.target;
    if (gamepadEnabled) {
      button.classList.remove(onClass);
      button.classList.add(offClass);
      animationFrameId = requestAnimationFrame(pollGamepad);
    } else {
      button.classList.add(onClass);
      button.classList.remove(offClass);
      cancelAnimationFrame(animationFrameId);
      animationFrameId = null;
    }
  }

  window.addEventListener("gamepadconnected", function(e) {
    console.log("***Gamepad Connected*** :", e.gamepad);
    document.getElementById("gamepad_btn").style.display = "inline-block";
  });
  window.addEventListener("gamepaddisconnected", function(e) {
    console.log("***Gamepad DisConnected*** :", e.gamepad);
    let button = document.getElementById("gamepad_btn");
    if (animationFrameId !== null) {
      cancelAnimationFrame(animationFrameId);
      animationFrameId = null;
    }
    button.classList.add(onClass);
    button.classList.remove(offClass);
    button.style.display = "none";
    gamepadEnabled = false;
  });

  let button = document.getElementById("gamepad_btn");
  button.classList.add('toggle-control');
  button.disabled = false;
  button.addEventListener('mousedown', onMouseDown);

  let lastButtonStates = [];
  let lastButtonValue = [];
  let prevAxes = [0, 0, 0, 0];

  function pollGamepad() {
    const gamepads = navigator.getGamepads();
    if (!gamepads) return;

    for (let gp of gamepads) {
      if (!gp) continue;

      gp.buttons.forEach((btn, i) => {
        const pressed = btn.pressed;
        const lastPressed = lastButtonStates[i] || false;
        let id = gp.id;
        // Buttons with JS index 6 and 7 are the trigger buttons, which are analog buttons.
        if (i == 6 || i == 7) {
          if (pressed || lastPressed) {
            const value = btn.value;
            const mappedValue = Math.min(trigger_value_range, Math.max(0, Math.round(value * trigger_value_range)));
            dc.sendGamepadMotion({button: ABS_CODES[i], value: mappedValue});
            console.debug(`***Button*** ${i} ***pressed down***`);
          }
        } else {
          if (pressed && !lastPressed) {
            dc.sendGamepadKey({id: id, button: i, down: true});
            console.debug(`***Button*** ${i} ***pressed down***`);
          } else if (!pressed && lastPressed) {
            dc.sendGamepadKey({id: id, button: i, down: false});
            console.debug(`***Button*** ${i} ***pressed up***`);
          }
        }
        lastButtonStates[i] = pressed;
      });

      gp.axes.forEach((axis, i) => {
        if (Math.abs(axis - prevAxes[i]) > 0.1) {
          const mappedAxis = Math.round((axis + 1.0) * 65535.0 / 2.0) - joystick_value_range;
          dc.sendGamepadMotion({button: ABS_CODES[i], value: mappedAxis});
          prevAxes[i] = axis;
          console.debug(`***Axis*** ${i}: ${axis.toFixed(2)}`);
        }
      });
    }

    animationFrameId = requestAnimationFrame(pollGamepad);
  }
}

