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
  let icon = document.createElement("span");
  icon.classList.add("toggle-control-icon");
  icon.classList.add("material-icons-outlined");
  if (iconName) {
    icon.appendChild(document.createTextNode(iconName));
  }
  elm.appendChild(icon);
  let toggle = document.createElement("label");
  toggle.classList.add("toggle-control-switch");
  let input = document.createElement("input");
  input.type = "checkbox";
  toggle.appendChild(input);
  let slider = document.createElement("span");
  slider.classList.add("toggle-control-slider");
  toggle.appendChild(slider);
  elm.classList.add("toggle-control");
  elm.appendChild(toggle);
  if (onChangeCb) {
    input.onchange = e => onChangeCb(e.target.checked);
  }
}
