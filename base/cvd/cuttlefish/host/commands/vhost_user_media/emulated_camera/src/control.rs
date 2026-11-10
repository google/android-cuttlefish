// Copyright 2026, The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//! Generic V4L2 control machinery.

use v4l2r::bindings;
use v4l2r::ioctl::CtrlWhich;
use virtio_media::ioctl::IoctlResult;

use crate::render_params::RenderParams;

/// Rust equivalent of the V4L2_CTRL_ID2WHICH C preprocessor macro.
/// Extracts the control class ID from a control ID by masking out the lower 16 bits
/// and any reserved top bits (uses mask 0x0fff0000).
/// See: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/control.html
pub const fn v4l2_ctrl_id2which(id: u32) -> u32 {
    id & 0x0fff0000
}

/// `V4L2_CID_*_CLASS` id of `class`.
const fn class_header_id(class: u32) -> u32 {
    class | 1
}

/// Formats an ASCII name for a `v4l2_query_ext_ctrl` or `v4l2_querymenu` response, NULL-padding
/// the remainder of the 32-byte array. If `name` is longer than 31 bytes it is truncated and
/// the last byte is still forced to NULL.
pub fn ctrl_name(name: &str) -> [u8; 32] {
    const MAX_LEN: usize = 32;
    let mut out = [0u8; MAX_LEN];
    let copy_len = std::cmp::min(name.len(), MAX_LEN - 1);
    out[..copy_len].copy_from_slice(&name.as_bytes()[..copy_len]);
    out
}

/// Name of a control class, as reported by the kernel.
fn class_name(class: u32) -> &'static str {
    match class {
        bindings::V4L2_CTRL_CLASS_USER => "User Controls",
        bindings::V4L2_CTRL_CLASS_CAMERA => "Camera Controls",
        bindings::V4L2_CTRL_CLASS_IMAGE_PROC => "Image Processing Controls",
        _ => unreachable!(
            "control class {:#x} has no name, add it to class_name()",
            class
        ),
    }
}

/// What `VIDIOC_QUERY_EXT_CTRL` reports for a control, besides its id.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ControlDescriptor {
    /// `V4L2_CTRL_TYPE_*`.
    pub type_: u32,
    /// Truncated to 31 bytes.
    pub name: &'static str,
    pub minimum: i64,
    pub maximum: i64,
    pub step: u64,
    pub default_value: i64,
    /// `V4L2_CTRL_FLAG_*`.
    pub flags: u32,
}

impl ControlDescriptor {
    fn is_64bit(&self) -> bool {
        self.type_ == bindings::v4l2_ctrl_type_V4L2_CTRL_TYPE_INTEGER64
    }

    fn elem_size(&self) -> u32 {
        if self.is_64bit() {
            std::mem::size_of::<i64>() as u32
        } else {
            std::mem::size_of::<i32>() as u32
        }
    }

    fn is_read_only(&self) -> bool {
        self.flags & bindings::V4L2_CTRL_FLAG_READ_ONLY != 0
    }
}

/// A V4L2 control.
pub trait Control: Send + Sync {
    /// `V4L2_CID_*`.
    fn id(&self) -> u32;

    fn descriptor(&self) -> ControlDescriptor;

    /// Current value.
    fn value(&self) -> i64;

    /// Validates `value` without applying it. Returns the `errno` reported to the guest.
    fn validate(&self, value: i64) -> Result<(), i32>;

    /// Validates and applies `value`. Returns whether the value changed.
    fn set(&mut self, value: i64) -> Result<bool, i32>;

    /// Name of menu entry `index`, `None` if there is no such entry.
    fn menu_name(&self, _index: u32) -> Option<&'static str> {
        None
    }

    /// Projects the value into `params`.
    fn apply(&self, _params: &mut RenderParams) {}
}

/// Result of a successful `VIDIOC_S_CTRL`.
pub struct SetControlOutcome {
    pub control: bindings::v4l2_control,
    /// Set if the value changed.
    pub event: Option<bindings::v4l2_event>,
}

/// The controls of a device.
pub struct ControlSet {
    /// Sorted by id.
    controls: Vec<Box<dyn Control>>,
    /// Classes of `controls`, sorted.
    classes: Vec<u32>,
}

impl ControlSet {
    /// Panics if two controls share an id.
    pub fn new(mut controls: Vec<Box<dyn Control>>) -> Self {
        controls.sort_by_key(|control| control.id());
        if let Some(pair) = controls
            .windows(2)
            .find(|pair| pair[0].id() == pair[1].id())
        {
            panic!("duplicate control id {:#x}", pair[0].id());
        }
        let mut classes: Vec<u32> = controls
            .iter()
            .map(|control| v4l2_ctrl_id2which(control.id()))
            .collect();
        classes.dedup();
        Self { controls, classes }
    }

    pub fn empty() -> Self {
        Self::new(Vec::new())
    }

    pub fn render_params(&self) -> RenderParams {
        let mut params = RenderParams::default();
        for control in &self.controls {
            control.apply(&mut params);
        }
        params
    }

    fn find(&self, id: u32) -> Option<&dyn Control> {
        self.controls
            .iter()
            .find(|control| control.id() == id)
            .map(|control| control.as_ref())
    }

    fn find_mut(&mut self, id: u32) -> Option<&mut Box<dyn Control>> {
        self.controls.iter_mut().find(|control| control.id() == id)
    }

    fn is_class_header(&self, id: u32) -> bool {
        self.classes
            .iter()
            .any(|class| class_header_id(*class) == id)
    }

    fn has_class(&self, class: u32) -> bool {
        self.classes.contains(&class)
    }

    /// Whether `id` is a control or a class header.
    pub fn knows(&self, id: u32) -> bool {
        self.is_class_header(id) || self.find(id).is_some()
    }

    /// Ids in `V4L2_CTRL_FLAG_NEXT_CTRL` order.
    fn enumeration(&self) -> impl Iterator<Item = u32> + '_ {
        let mut ids: Vec<u32> = self
            .classes
            .iter()
            .map(|class| class_header_id(*class))
            .chain(self.controls.iter().map(|control| control.id()))
            .collect();
        ids.sort_unstable();
        ids.into_iter()
    }

    fn class_header_query_ext_ctrl(class: u32) -> bindings::v4l2_query_ext_ctrl {
        bindings::v4l2_query_ext_ctrl {
            id: class_header_id(class),
            type_: bindings::v4l2_ctrl_type_V4L2_CTRL_TYPE_CTRL_CLASS,
            name: ctrl_name(class_name(class)).map(|b| b as i8),
            // A control class holds no value of its own, so it can be neither read nor
            // written. This mirrors what `v4l2_ctrl_fill()` does in the kernel.
            flags: bindings::V4L2_CTRL_FLAG_READ_ONLY | bindings::V4L2_CTRL_FLAG_WRITE_ONLY,
            elems: 1,
            elem_size: std::mem::size_of::<i32>() as u32,
            ..Default::default()
        }
    }

    fn control_query_ext_ctrl(control: &dyn Control) -> bindings::v4l2_query_ext_ctrl {
        let descriptor = control.descriptor();
        bindings::v4l2_query_ext_ctrl {
            id: control.id(),
            type_: descriptor.type_,
            name: ctrl_name(descriptor.name).map(|b| b as i8),
            minimum: descriptor.minimum,
            maximum: descriptor.maximum,
            step: descriptor.step,
            default_value: descriptor.default_value,
            flags: descriptor.flags,
            elems: 1,
            elem_size: descriptor.elem_size(),
            ..Default::default()
        }
    }

    fn query(&self, id: u32) -> Option<bindings::v4l2_query_ext_ctrl> {
        if self.is_class_header(id) {
            Some(Self::class_header_query_ext_ctrl(v4l2_ctrl_id2which(id)))
        } else {
            self.find(id).map(Self::control_query_ext_ctrl)
        }
    }

    /// `VIDIOC_QUERY_EXT_CTRL`. With `next` the first id greater than `id` is reported.
    ///
    /// https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-queryctrl.html
    pub fn query_ext_ctrl(
        &self,
        id: u32,
        next: bool,
    ) -> IoctlResult<bindings::v4l2_query_ext_ctrl> {
        let id = if next {
            self.enumeration()
                .find(|candidate| *candidate > id)
                .ok_or(libc::EINVAL)?
        } else {
            id
        };
        self.query(id).ok_or(libc::EINVAL)
    }

    /// `VIDIOC_QUERYMENU`.
    pub fn querymenu(&self, id: u32, index: u32) -> IoctlResult<bindings::v4l2_querymenu> {
        let name = self
            .find(id)
            .and_then(|control| control.menu_name(index))
            .ok_or(libc::EINVAL)?;
        Ok(bindings::v4l2_querymenu {
            id,
            index,
            __bindgen_anon_1: bindings::v4l2_querymenu__bindgen_ty_1 {
                name: ctrl_name(name),
            },
            ..Default::default()
        })
    }

    /// `VIDIOC_G_CTRL`.
    pub fn g_ctrl(&self, id: u32) -> IoctlResult<bindings::v4l2_control> {
        if self.is_class_header(id) {
            return Err(libc::EACCES);
        }
        let control = self.find(id).ok_or(libc::EINVAL)?;
        Ok(bindings::v4l2_control {
            id,
            value: control.value() as i32,
        })
    }

    /// `VIDIOC_S_CTRL`.
    pub fn s_ctrl(&mut self, id: u32, value: i32) -> IoctlResult<SetControlOutcome> {
        if self.is_class_header(id) {
            return Err(libc::EACCES);
        }
        let control = self.find_mut(id).ok_or(libc::EINVAL)?;
        if control.descriptor().is_read_only() {
            return Err(libc::EACCES);
        }
        let changed = control.set(value as i64)?;
        let control = bindings::v4l2_control {
            id,
            value: control.value() as i32,
        };
        let event = if changed {
            Some(self.ctrl_event(id)?)
        } else {
            None
        };
        Ok(SetControlOutcome { control, event })
    }

    /// Validates `which` and, for a class, that every control in `ctrl_array` belongs to it.
    fn check_which(
        &self,
        which: &CtrlWhich,
        allow_default: bool,
        ctrls: &mut bindings::v4l2_ext_controls,
        ctrl_array: &[bindings::v4l2_ext_control],
        per_control_error_idx: bool,
    ) -> IoctlResult<()> {
        match which {
            CtrlWhich::Current => {}
            CtrlWhich::Default if allow_default => {}
            CtrlWhich::Class(class) if self.has_class(*class) => {
                for (idx, ctrl) in ctrl_array.iter().enumerate() {
                    if v4l2_ctrl_id2which(ctrl.id) != *class {
                        ctrls.error_idx = if per_control_error_idx {
                            idx as u32
                        } else {
                            ctrls.count
                        };
                        return Err(libc::EINVAL);
                    }
                }
            }
            _ => {
                ctrls.error_idx = ctrls.count;
                return Err(libc::EINVAL);
            }
        }
        Ok(())
    }

    fn read_value(ctrl: &bindings::v4l2_ext_control, descriptor: &ControlDescriptor) -> i64 {
        unsafe {
            if descriptor.is_64bit() {
                ctrl.__bindgen_anon_1.value64
            } else {
                ctrl.__bindgen_anon_1.value as i64
            }
        }
    }

    fn write_value(
        ctrl: &mut bindings::v4l2_ext_control,
        descriptor: &ControlDescriptor,
        value: i64,
    ) {
        if descriptor.is_64bit() {
            ctrl.__bindgen_anon_1.value64 = value;
        } else {
            ctrl.__bindgen_anon_1.value = value as i32;
        }
    }

    /// `VIDIOC_G_EXT_CTRLS`.
    pub fn g_ext_ctrls(
        &self,
        which: CtrlWhich,
        ctrls: &mut bindings::v4l2_ext_controls,
        ctrl_array: &mut [bindings::v4l2_ext_control],
    ) -> IoctlResult<()> {
        self.check_which(&which, true, ctrls, ctrl_array, false)?;

        for ctrl in ctrl_array.iter_mut() {
            // Class headers hold no value and must fail on read.
            if self.is_class_header(ctrl.id) {
                ctrls.error_idx = ctrls.count;
                return Err(libc::EACCES);
            }
            let Some(control) = self.find(ctrl.id) else {
                ctrls.error_idx = ctrls.count;
                return Err(libc::EINVAL);
            };
            let descriptor = control.descriptor();
            let value = match which {
                CtrlWhich::Default => descriptor.default_value,
                _ => control.value(),
            };
            Self::write_value(ctrl, &descriptor, value);
        }
        ctrls.error_idx = ctrls.count;
        Ok(())
    }

    /// `VIDIOC_TRY_EXT_CTRLS`.
    pub fn try_ext_ctrls(
        &self,
        which: CtrlWhich,
        ctrls: &mut bindings::v4l2_ext_controls,
        ctrl_array: &mut [bindings::v4l2_ext_control],
    ) -> IoctlResult<()> {
        self.check_which(&which, false, ctrls, ctrl_array, true)?;

        for (idx, ctrl) in ctrl_array.iter().enumerate() {
            // Class headers hold no value and must fail on write.
            if self.is_class_header(ctrl.id) {
                ctrls.error_idx = idx as u32;
                return Err(libc::EACCES);
            }
            let Some(control) = self.find(ctrl.id) else {
                ctrls.error_idx = idx as u32;
                return Err(libc::EINVAL);
            };
            let descriptor = control.descriptor();
            if descriptor.is_read_only() {
                ctrls.error_idx = idx as u32;
                return Err(libc::EACCES);
            }
            if let Err(err) = control.validate(Self::read_value(ctrl, &descriptor)) {
                ctrls.error_idx = idx as u32;
                return Err(err);
            }
        }
        ctrls.error_idx = ctrls.count;
        Ok(())
    }

    /// `VIDIOC_S_EXT_CTRLS`. Returns the events of the controls whose value changed.
    pub fn s_ext_ctrls(
        &mut self,
        which: CtrlWhich,
        ctrls: &mut bindings::v4l2_ext_controls,
        ctrl_array: &mut [bindings::v4l2_ext_control],
    ) -> IoctlResult<Vec<bindings::v4l2_event>> {
        // Validate everything first so that the set is applied atomically.
        if let Err(err) = self.try_ext_ctrls(which, ctrls, ctrl_array) {
            ctrls.error_idx = ctrls.count;
            return Err(err);
        }

        let mut events = Vec::new();
        for ctrl in ctrl_array.iter_mut() {
            let id = ctrl.id;
            let Some(control) = self.find_mut(id) else {
                continue;
            };
            let descriptor = control.descriptor();
            let value = Self::read_value(ctrl, &descriptor);
            if let Ok(true) = control.set(value) {
                if let Ok(event) = self.ctrl_event(id) {
                    events.push(event);
                }
            }
        }
        ctrls.error_idx = ctrls.count;
        Ok(events)
    }

    /// Builds the `V4L2_EVENT_CTRL` payload describing the current state of `id`.
    pub fn ctrl_event(&self, id: u32) -> IoctlResult<bindings::v4l2_event> {
        let control = self.find(id).ok_or(libc::EINVAL)?;
        let descriptor = control.descriptor();
        let mut event = bindings::v4l2_event {
            type_: bindings::V4L2_EVENT_CTRL,
            id,
            ..Default::default()
        };
        // Listeners gate on this mask, so an event without it is silently ignored.
        event.u.ctrl.changes = bindings::V4L2_EVENT_CTRL_CH_VALUE;
        event.u.ctrl.type_ = descriptor.type_;
        if descriptor.is_64bit() {
            event.u.ctrl.__bindgen_anon_1.value64 = control.value();
        } else {
            event.u.ctrl.__bindgen_anon_1.value = control.value() as i32;
        }
        event.u.ctrl.flags = descriptor.flags;
        event.u.ctrl.minimum = descriptor.minimum as i32;
        event.u.ctrl.maximum = descriptor.maximum as i32;
        event.u.ctrl.step = descriptor.step as i32;
        event.u.ctrl.default_value = descriptor.default_value as i32;
        Ok(event)
    }

    /// `VIDIOC_SUBSCRIBE_EVENT` for `V4L2_EVENT_CTRL` on `id`. Returns the initial event, `None`
    /// for class headers.
    pub fn subscribe_ctrl_event(&self, id: u32) -> IoctlResult<Option<bindings::v4l2_event>> {
        if self.is_class_header(id) {
            return Ok(None);
        }
        self.ctrl_event(id).map(Some)
    }
}
