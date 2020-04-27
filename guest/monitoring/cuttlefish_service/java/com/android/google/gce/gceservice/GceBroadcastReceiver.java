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
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.net.ConnectivityManager;
import android.util.Log;


public class GceBroadcastReceiver extends BroadcastReceiver {
    private static final String LOG_TAG = "GceBroadcastReceiver";


    private void reportIntent(Context context, String intentType) {
        Intent intent = new Intent(context, GceService.class);
        intent.setAction(intentType);
        context.startForegroundService(intent);
    }


    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent != null) {
            final String action = intent.getAction();
            Log.i(LOG_TAG, "Received broadcast: " + action);

            if (action.equals(Intent.ACTION_BOOT_COMPLETED)) {
                reportIntent(context, GceService.INTENT_ACTION_CONFIGURE);
            } else if (action.equals(ConnectivityManager.CONNECTIVITY_ACTION)) {
                reportIntent(context, GceService.INTENT_ACTION_NETWORK_CHANGED);
            } else if (action.equals(BluetoothAdapter.ACTION_STATE_CHANGED)) {
                reportIntent(context, GceService.INTENT_ACTION_BLUETOOTH_CHANGED);
            }
        }
    }
}
