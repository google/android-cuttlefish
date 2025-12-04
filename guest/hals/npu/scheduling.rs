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

use crate::commands::InferenceOptions;
use android_hardware_npu::aidl::android::hardware::npu::{
    EndReason::EndReason,
    IScheduling::IScheduling,
    ISchedulingCallback::{ISchedulingCallback, DEBOUNCE_DURATION_MS},
    SchedulingConfig::{SchedulingConfig, MAX_PRIORITY, MIN_PRIORITY},
    StartReason::StartReason,
    WorkInfo::WorkInfo,
};
use anyhow::{anyhow, Context, Result};
use binder::{ExceptionCode, Interface, Status, Strong};
use log::{error, info, warn};
use nix::{
    sys::time::TimeValLike,
    time::{clock_gettime, ClockId},
};
use std::{
    collections::{BinaryHeap, HashMap, HashSet},
    sync::{
        atomic::{AtomicI32, Ordering},
        mpsc::{channel, Sender},
        Arc, Mutex,
    },
    thread::{self, spawn, JoinHandle},
    time::Duration,
};

const DEFAULT_CONFIG: SchedulingConfig = SchedulingConfig {
    uid: 0, // Not used.
    priority: 1000,
    hasDirectAccess: true,
    canAttributeOtherUid: false,
};
#[derive(Clone, Debug)]
struct WorkRequest {
    info: WorkInfo,
    sender: Sender<Result<()>>,
}
impl Ord for WorkRequest {
    fn cmp(&self, other: &WorkRequest) -> std::cmp::Ordering {
        let a = self.info.effectivePriority;
        let b = other.info.effectivePriority;
        if a < b {
            std::cmp::Ordering::Greater
        } else if a > b {
            std::cmp::Ordering::Less
        } else {
            std::cmp::Ordering::Equal
        }
    }
}
impl PartialOrd for WorkRequest {
    fn partial_cmp(&self, other: &WorkRequest) -> std::option::Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}
impl Eq for WorkRequest {}
impl PartialEq for WorkRequest {
    fn eq(&self, other: &WorkRequest) -> bool {
        self.info.effectivePriority == other.info.effectivePriority
    }
}
#[derive(Clone, Debug)]
struct WorkEndData {
    info: WorkInfo,
    reason: EndReason,
}
#[derive(Default, Debug)]
struct SchedulingData {
    callback: Option<Strong<dyn ISchedulingCallback>>,
    configs: HashMap<i32, SchedulingConfig>,
    queue: BinaryHeap<WorkRequest>,
    debounce_map: HashMap<i32, WorkEndData>,
    next_id: AtomicI32,
}
impl SchedulingData {
    fn is_debounced(&self, uid: &i32) -> bool {
        self.debounce_map.contains_key(uid)
    }

    fn next_id(&mut self) -> i32 {
        self.next_id.fetch_add(1, Ordering::AcqRel)
    }
}
pub struct SchedulingService {
    data: Arc<Mutex<SchedulingData>>,
    worker_handle: JoinHandle<()>,
}
fn check_configs(configs: &[SchedulingConfig]) -> Result<(), Status> {
    let mut uids = HashSet::new();
    for config in configs.iter() {
        if !uids.insert(config.uid) {
            error!("Duplicate UID found: {}", config.uid);
            return Err(Status::new_exception(ExceptionCode::ILLEGAL_ARGUMENT, None));
        }

        if config.priority < MIN_PRIORITY || config.priority > MAX_PRIORITY {
            error!("Priority {} is out of range", config.priority);
            return Err(Status::new_exception(ExceptionCode::ILLEGAL_ARGUMENT, None));
        }
    }
    Ok(())
}
struct SchedulingServiceProxy {
    service: Arc<SchedulingService>,
}
impl IScheduling for SchedulingServiceProxy {
    fn setSchedulingConfigs(&self, configs: &[SchedulingConfig]) -> Result<(), Status> {
        self.service.setSchedulingConfigs(configs)
    }
    fn updateSchedulingConfigs(&self, configs: &[SchedulingConfig]) -> Result<(), Status> {
        self.service.updateSchedulingConfigs(configs)
    }
    fn setCallback(
        &self,
        callback: Option<&Strong<dyn ISchedulingCallback>>,
    ) -> Result<(), Status> {
        self.service.setCallback(callback)
    }
}

impl Interface for SchedulingServiceProxy {}
fn now_in_millis() -> Result<i64> {
    Ok(clock_gettime(ClockId::CLOCK_MONOTONIC)?.num_milliseconds())
}

fn debounce_loop(data: Arc<Mutex<SchedulingData>>) -> Result<()> {
    let mut park_timeout_ms = u32::MAX;
    loop {
        thread::park_timeout_ms(park_timeout_ms);

        park_timeout_ms = u32::MAX;
        let now = now_in_millis().unwrap_or_default();
        let mut data = data.lock().unwrap();
        let mut expired = Vec::new();
        data.debounce_map.retain(|_uid, work_end| {
            let diff = now - work_end.info.timestampMs;
            if diff < DEBOUNCE_DURATION_MS as i64 {
                // Still debouncing, find out of this is the shortest duration or not
                // and use that as the wait time if so.
                park_timeout_ms =
                    std::cmp::min(park_timeout_ms, (DEBOUNCE_DURATION_MS - diff as i32) as u32);
                true
            } else {
                expired.push(work_end.clone());
                false
            }
        });
        if let Some(ref cb) = data.callback {
            for work_end in expired {
                if let Err(e) = cb.onWorkEnded(&work_end.info, work_end.reason) {
                    warn!("Failed to call onWorkEnded: {e:?}");
                } else {
                    info!("onWorkEnded: {:?}", &work_end.info);
                }
            }
        }
    }
}

fn worker_loop(
    scheduling_data: Arc<Mutex<SchedulingData>>,
    debounce_handle: JoinHandle<()>,
) -> Result<()> {
    loop {
        let mut data = scheduling_data.lock().unwrap();
        let request = if let Some(request) = data.queue.pop() {
            request
        } else {
            drop(data);
            thread::park();
            continue;
        };

        let mut info = request.info.clone();
        if !data.is_debounced(&info.uid) {
            if let Some(cb) = data.callback.clone() {
                info.id = data.next_id();
                info.timestampMs = now_in_millis()?;
                if let Err(e) = cb.onWorkStarted(&info, StartReason::INITIAL) {
                    warn!("Failed to call onWorkStarted: {e:?}");
                } else {
                    info!("onWorkStarted: {:?}", &info);
                }
            }
        }

        drop(data);
        thread::sleep(Duration::from_millis(10));

        let mut data = scheduling_data.lock().unwrap();
        info.id = data.next_id();
        info.timestampMs = now_in_millis()?;

        data.debounce_map.insert(info.uid, WorkEndData { info, reason: EndReason::COMPLETED });
        drop(data);

        debounce_handle.thread().unpark();
        request.sender.send(Ok(()))?;
    }
}
impl SchedulingService {
    pub fn new() -> Self {
        let data = Arc::new(Mutex::new(SchedulingData::default()));
        let worker_data = data.clone();
        let debounce_data = data.clone();

        let debouncer_handle = spawn(move || {
            if let Err(e) = debounce_loop(debounce_data) {
                error!("debounce_loop failed: {e}");
            }
        });

        let worker_handle = spawn(move || {
            if let Err(e) = worker_loop(worker_data, debouncer_handle) {
                error!("worker_loop failed: {e}");
            }
        });

        Self { data, worker_handle }
    }
    pub fn run_test_inference(
        &self,
        calling_uid: i32,
        calling_pid: i32,
        options: &InferenceOptions,
    ) -> Result<()> {
        let receivers = {
            let mut data = self.data.lock().unwrap();
            let config = data.configs.get(&calling_uid).unwrap_or(&DEFAULT_CONFIG);
            if !config.hasDirectAccess {
                error!("Rejecting access from {calling_uid} due to config");
                return Err(anyhow!("Direct access denied"));
            }

            let mut app_priority = config.priority;
            if options.original_uid >= 0 && options.original_uid != calling_uid {
                if !config.canAttributeOtherUid {
                    error!("Rejecting access from {calling_uid} due to config");
                    return Err(anyhow!("Cannot attribute other uid"));
                }

                if let Some(config) = data.configs.get(&options.original_uid) {
                    app_priority = config.priority;
                }
            }

            let mut receivers = Vec::new();

            let mut info = WorkInfo {
                id: data.next_id(),
                groupId: None,
                uid: calling_uid,
                debugPid: calling_pid,
                originalUid: options.original_uid,
                debugFeatureId: None,
                jobPriority: options.priority,
                effectivePriority: options.priority + app_priority,
                timestampMs: now_in_millis()?,
                deviceNumber: 0,
            };

            if !data.is_debounced(&calling_uid) {
                if let Some(ref cb) = data.callback {
                    if let Err(e) = cb.onWorkRequested(&info) {
                        warn!("Failed to call onWorkRequested: {e:?}");
                    } else {
                        info!("onWorkRequested: {:?}", &info);
                    }
                }
            }

            for _ in 0..options.num_requests {
                let (sender, receiver) = channel();

                info.id = data.next_id();
                info.timestampMs = now_in_millis()?;

                data.queue.push(WorkRequest { info: info.clone(), sender });
                receivers.push(receiver);
            }

            self.worker_handle.thread().unpark();

            receivers
        };

        // Wait for the results
        for receiver in receivers {
            receiver.recv().context("Failed to receive result from worker")??;
        }

        Ok(())
    }
    pub fn create_service_proxy(target: Arc<Self>) -> impl IScheduling {
        SchedulingServiceProxy { service: target }
    }
}
impl IScheduling for SchedulingService {
    fn setSchedulingConfigs(&self, configs: &[SchedulingConfig]) -> Result<(), Status> {
        info!("setConfigs called {configs:?}");
        check_configs(configs)?;
        let config_map = &mut self.data.lock().unwrap().configs;
        config_map.clear();
        for config in configs.iter() {
            config_map.insert(config.uid, config.clone());
        }
        info!("setConfigs finished {configs:?}");
        Ok(())
    }
    fn updateSchedulingConfigs(&self, configs: &[SchedulingConfig]) -> Result<(), Status> {
        info!("updateConfigs called {configs:?}");
        check_configs(configs)?;
        let config_map = &mut self.data.lock().unwrap().configs;
        for config in configs.iter() {
            config_map.insert(config.uid, config.clone());
        }
        Ok(())
    }
    fn setCallback(
        &self,
        callback: Option<&Strong<dyn ISchedulingCallback>>,
    ) -> Result<(), Status> {
        info!("setCallback called {callback:?}");
        self.data.lock().unwrap().callback = callback.cloned();
        Ok(())
    }
}
impl Interface for SchedulingService {}
