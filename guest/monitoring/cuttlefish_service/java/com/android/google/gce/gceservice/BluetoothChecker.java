/*
 * Copyright (C) 2017 The Android Open Source Project
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
package com.android.google.gce.gceservice;

import android.bluetooth.BluetoothAdapter;
import android.content.Context;
import android.content.pm.PackageManager;
import android.content.res.Resources;
import android.util.Log;

/*
 * A job that checks for Bluetooth being enabled before reporting VIRTUAL_DEVICE_BOOT_COMPLETED. Our
 * devices should always boot with bt enabled, it can be configured in
 * gce_x86/overlay_<device>/frameworks/base/packages/SettingsProvider/res/values/defaults.xml
 */
public class BluetoothChecker extends JobBase {
    private static final String LOG_TAG = "GceBluetoothChecker";
    private final GceFuture<Boolean> mEnabled = new GceFuture<Boolean>("Bluetooth");


    public BluetoothChecker(Context context) {
        super(LOG_TAG);
        PackageManager pm = context.getPackageManager();
        boolean hasBluetooth = pm.hasSystemFeature(PackageManager.FEATURE_BLUETOOTH);
        if (!hasBluetooth) {
            Log.i(LOG_TAG, "Bluetooth checker disabled");
            mEnabled.set(false);
        }
    }


    @Override
    public int execute() {
        if (mEnabled.isDone()) {
            return 0;
        }
        BluetoothAdapter bluetoothAdapter = BluetoothAdapter.getDefaultAdapter();
        if (bluetoothAdapter == null) {
            Log.e(LOG_TAG, "No bluetooth adapter found");
            mEnabled.set(new Exception("No bluetooth adapter found"));
        } else {
            if (bluetoothAdapter.isEnabled()) {
                Log.i(LOG_TAG, "Bluetooth enabled with name: " + bluetoothAdapter.getName());
                mEnabled.set(true);
            } else {
                Log.i(LOG_TAG, "Bluetooth disabled with name: " + bluetoothAdapter.getName());
            }
        }
        return 0;
    }


    @Override
    public void onDependencyFailed(Exception e) {
        mEnabled.set(e);
    }


    public GceFuture<Boolean> getEnabled() {
        return mEnabled;
    }
}
