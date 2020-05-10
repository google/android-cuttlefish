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

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import android.net.wifi.SupplicantState;
import android.net.wifi.WifiConfiguration;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.Handler;
import android.util.Log;
import java.util.ArrayList;
import java.util.List;

/**
 * Manage WIFI state.
 */
public class GceWifiManager extends JobBase {
    private static final String LOG_TAG = "GceWifiManager";
    /* Timeout after which the service will check if wifi has come up. */
    private static final int WIFI_RECONNECTION_TIMEOUT_S = 5;
    private static final String WIFI_CONNECTED_MESSAGE =
        "VIRTUAL_DEVICE_NETWORK_WIFI_CONNECTED";

    private final JobExecutor mJobExecutor;
    private final Context mContext;
    private final WifiManager mWifiManager;
    private final ConnectivityManager mConnManager;
    private final BootReporter mBootReporter;

    private final MonitorWifiJob mMonitorWifiJob;


    public GceWifiManager(Context context, BootReporter bootReporter, JobExecutor executor) {
        super(LOG_TAG);

        mContext = context;
        mWifiManager = (WifiManager)context.getSystemService(Context.WIFI_SERVICE);
        mConnManager = (ConnectivityManager)context.getSystemService(Context.CONNECTIVITY_SERVICE);
        mBootReporter = bootReporter;
        mJobExecutor = executor;
        mMonitorWifiJob = new MonitorWifiJob();
    }


    /** Executed during initial configuration. */
    @Override
    public synchronized int execute() {
        mJobExecutor.schedule(mMonitorWifiJob);
        return 0;
    }


    @Override
    public void onDependencyFailed(Exception e) {
        Log.e(LOG_TAG, "Initial WIFI configuration failed due to dependency.", e);
        getWifiReady().set(e);
    }


    public GceFuture<Boolean> getWifiReady() {
        return mMonitorWifiJob.getWifiReady();
    }

    private class MonitorWifiJob extends JobBase {
        private final GceFuture<Boolean> mWifiReady =
                new GceFuture<Boolean>("WIFI Ready");
        private boolean mReportedWifiNotConnected = false;


        public MonitorWifiJob() {
            super(LOG_TAG);
        }


        public synchronized void cancel() {
            if (!mWifiReady.isDone()) {
                mWifiReady.cancel(false);
            }
        }


        @Override
        public synchronized int execute() {
            // Could be cancelled or exception.
            if (mWifiReady.isDone()) return 0;

            WifiInfo info = mWifiManager.getConnectionInfo();
            if (info.getSupplicantState() != SupplicantState.COMPLETED) {
                if (!mReportedWifiNotConnected) {
                    Log.w(LOG_TAG, "Wifi not yet connected.");
                    mReportedWifiNotConnected = true;
                }
                return WIFI_RECONNECTION_TIMEOUT_S;
            } else {
                mBootReporter.reportMessage(WIFI_CONNECTED_MESSAGE);
                Log.i(LOG_TAG, "Wifi connected.");
                mWifiReady.set(true);
                return 0;
            }
        }


        @Override
        public void onDependencyFailed(Exception e) {
            Log.e(LOG_TAG, "Wifi state change failed due to failing dependency.", e);
            mWifiReady.set(e);
        }


        public GceFuture<Boolean> getWifiReady() {
            return mWifiReady;
        }
    }
}
