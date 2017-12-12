/*
 * Copyright (C) 2016 The Android Open Source Project
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
 * Manage WIFI state according to Metadata Server reported desired state.
 */
public class GceWifiManager extends JobBase implements MetadataClient.OnAttributeUpdateListener {
    private static final String LOG_TAG = "GceWifiManager";
    /* Metadata attribute controlling WLAN state. */
    private static final String METADATA_INSTANCE_ATTRIBUTE_WIFI = "cfg_sta_wlan_state";
    private static final String METADATA_INSTANCE_ATTRIBUTE_WIFI_ENABLED = "ENABLED";
    /* Timeout after which another attempt to re-connect wifi will be made. */
    private static final int WIFI_RECONNECTION_TIMEOUT_S = 3;
    /* Maximum number of retries before giving up and marking WIFI as inoperable. */
    private static final int WIFI_RECONNECTION_MAX_ATTEMPTS = 10;

    /** Describes possible WIFI states.
     * WifiState is:
     * - UNKNOWN only at the initialization time, replaced with state from
     *           from Android's WifiManager.
     * - ENABLED when WIFI is connected and operational,
     * - DISABLED when WIFI is turned off,
     * - FAILED if GceWifiManager was unable to configure WIFI.
     */
    public enum WifiState {
        DISABLED,
        ENABLED;
    };

    private final JobExecutor mJobExecutor;
    private final Context mContext;
    private final WifiManager mWifiManager;
    private final ConnectivityManager mConnManager;
    private final MetadataClient mMetadataClient;

    private ConfigureWifi mConfigureWifiJob = new ConfigureWifi();
    private SetWifiState mSetInitialWifiStateJob = new SetWifiState();
    private SetWifiState mSetRuntimeWifiStateJob = null;

    private String mInitialWifiState = null;


    /** Constructor.
    */
    public GceWifiManager(Context context, JobExecutor executor, MetadataClient client) {
        super(LOG_TAG);

        mContext = context;
        mWifiManager = (WifiManager)context.getSystemService(Context.WIFI_SERVICE);
        mConnManager = (ConnectivityManager)context.getSystemService(Context.CONNECTIVITY_SERVICE);
        mJobExecutor = executor;
        mMetadataClient = client;
        mMetadataClient.addOnAttributeUpdateListener(
                METADATA_INSTANCE_ATTRIBUTE_WIFI, this);
    }


    private boolean isMobileNetworkAvailable() {
        for (NetworkInfo network : mConnManager.getAllNetworkInfo()) {
            if (network.getType() == ConnectivityManager.TYPE_MOBILE) return true;
        }
        return false;
    }


    private WifiState getWifiStateFromMetadata(String value) {
        if (value == null) {
            if (isMobileNetworkAvailable()) {
                return WifiState.DISABLED;
            } else {
                return WifiState.ENABLED;
            }
        } else if (value.equals(METADATA_INSTANCE_ATTRIBUTE_WIFI_ENABLED)) {
            return WifiState.ENABLED;
        } else {
            return WifiState.DISABLED;
        }
    }


    /** Executed during initial configuration.
    */
    @Override
    public synchronized int execute() {
        // At this point, mInitialWifiState may be null, which is acceptable.
        WifiState initialState = getWifiStateFromMetadata(mInitialWifiState);
        mSetInitialWifiStateJob.setState(initialState);

        // Only configure wifi if expected state is ENABLED.
        // Configuring wifi *requires* wpa_supplicant to be up.
        // This means that in order to configure wifi, we have to enable it first.
        if (initialState == WifiState.ENABLED) {
            mJobExecutor.schedule(mConfigureWifiJob, mMetadataClient.getMetadataReady());
            mJobExecutor.schedule(mSetInitialWifiStateJob, mConfigureWifiJob.getWifiConfigured());
        } else {
            // If initial state is DISABLED, there's no need to wait for Wifi configuration to
            // complete. Just shut it off.
            mJobExecutor.schedule(mSetInitialWifiStateJob);
        }
        return 0;
    }


    @Override
    public void onDependencyFailed(Exception e) {
        Log.e(LOG_TAG, "Initial WIFI configuration failed due to dependency.", e);
        getInitialWifiStateChangeReady().set(e);
    }


    /* MetadataClient.OnInstanceAttributeUpdateListener Interface method.
     * Called by MetadataClient when Metadata Server reports value change
     * for specific key.
     */
    @Override
    public synchronized void onAttributeUpdate(MetadataAttribute attribute) {
        WifiState runtimeState = getWifiStateFromMetadata(attribute.getValue());

        mInitialWifiState = attribute.getInitialValue();

        if (mSetRuntimeWifiStateJob != null) {
            mSetRuntimeWifiStateJob.cancel();
        }

        mSetRuntimeWifiStateJob = new SetWifiState();
        mSetRuntimeWifiStateJob.setState(runtimeState);

        // In case we never configured WIFI in the first place (initial state == DISABLED)
        // configure it now.
        if (!mConfigureWifiJob.getWifiConfigured().isDone())
            mJobExecutor.schedule(mConfigureWifiJob);

        mJobExecutor.schedule(mSetRuntimeWifiStateJob,
                mConfigureWifiJob.getWifiConfigured(), mSetInitialWifiStateJob.getWifiReady());
    }


    public GceFuture<Boolean> getInitialWifiStateChangeReady() {
        // NOTE: this variable is initialized at construction time:
        // a call to addOnAttributeUpdateListener() triggers immediate callback from
        // MetadataClient with initial & actual MetadataAttribute value.
        return mSetInitialWifiStateJob.getWifiReady();
    }


    /* Configure WIFI network stack.
     *
     * Adds network configuration that covers AndroidWifi virtual hotspot.
     */
    private class ConfigureWifi extends JobBase {
        private final GceFuture<Boolean> mWifiConfigured =
                new GceFuture<Boolean>("WIFI Configured");
        private boolean mReportedWaitingForSupplicant = false;


        public ConfigureWifi() {
            super(LOG_TAG);
        }


        @Override
        public int execute() {
            if (mWifiConfigured.isDone()) return 0;

            if (!mWifiManager.pingSupplicant()) {
                if (!mWifiManager.isWifiEnabled()) {
                    mWifiManager.setWifiEnabled(true);
                }
                if (!mReportedWaitingForSupplicant) {
                    Log.i(LOG_TAG, "Supplicant not ready.");
                    mReportedWaitingForSupplicant = true;
                }
                return WIFI_RECONNECTION_TIMEOUT_S;
            }

            WifiConfiguration conf = new WifiConfiguration();
            conf.SSID = "\"AndroidWifi\"";
            conf.allowedKeyManagement.set(WifiConfiguration.KeyMgmt.NONE);
            int network_id = mWifiManager.addNetwork(conf);
            if (network_id < 0) {
                Log.e(LOG_TAG, "Could not update wifi network.");
                mWifiConfigured.set(new Exception("Could not add WIFI network"));
            } else {
                mWifiManager.enableNetwork(network_id, false);
                mWifiConfigured.set(true);
            }
            return 0;
        }


        @Override
        public void onDependencyFailed(Exception e) {
            Log.e(LOG_TAG, "Could not configure WIFI.", e);
            mWifiConfigured.set(e);
        }


        public GceFuture<Boolean> getWifiConfigured() {
            return mWifiConfigured;
        }
    }


    /* Modifies Wifi state:
     * - if wifi disable requested (state == false), simply turns off wifi.
     * - if wifi enable requested (state == true), turns on wifi and arms the
     *   connection timeout (see startWifiReconnectionTimeout).
     */
    private class SetWifiState extends JobBase {
        private final GceFuture<Boolean> mWifiReady =
                new GceFuture<Boolean>("WIFI Ready");
        private WifiState mDesiredState = WifiState.DISABLED;
        private int mWifiStateChangeAttempt = 0;
        private boolean mReportedWifiNotConnected = false;


        public SetWifiState() {
            super(LOG_TAG);
        }


        public void setState(WifiState state) {
            mDesiredState = state;
        }


        public synchronized void cancel() {
            if (!mWifiReady.isDone()) {
                mWifiReady.cancel(false);
            }
        }


        @Override
        public synchronized int execute() {
            WifiState currentState = mWifiManager.isWifiEnabled() ?
                    WifiState.ENABLED : WifiState.DISABLED;

            // Could be cancelled or exception.
            if (mWifiReady.isDone()) return 0;

            if (mWifiStateChangeAttempt >= WIFI_RECONNECTION_MAX_ATTEMPTS) {
                mWifiReady.set(new Exception(
                        String.format("Unable to change wifi state after %d attempts.",
                            WIFI_RECONNECTION_MAX_ATTEMPTS)));
                return 0;
            }

            if (currentState == mDesiredState) {
                switch (currentState) {
                    case ENABLED:
                        // Wifi is enabled, but probably not yet connected. Check.
                        WifiInfo info = mWifiManager.getConnectionInfo();
                        if (info.getSupplicantState() != SupplicantState.COMPLETED) {
                            if (!mReportedWifiNotConnected) {
                                Log.w(LOG_TAG, "Wifi not yet connected.");
                                mReportedWifiNotConnected = true;
                            }
                        } else {
                            Log.i(LOG_TAG, "Wifi connected.");
                            mWifiReady.set(true);
                        }
                        break;

                    case DISABLED:
                        // There's nothing extra to check for disable wifi.
                        mWifiReady.set(true);
                        break;
                }

                if (mWifiReady.isDone()) {
                    return 0;
                }
            }

            // At this point we know that:
            // - current state is different that desired state, or
            // - current state is enabled, but wifi is not yet connected.
            ++mWifiStateChangeAttempt;

            switch (mDesiredState) {
                case DISABLED:
                    mWifiManager.setWifiEnabled(false);
                    break;

                case ENABLED:
                    mWifiManager.setWifiEnabled(true);
                    mWifiManager.reconnect();
                    break;
            }
            return WIFI_RECONNECTION_TIMEOUT_S;
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
