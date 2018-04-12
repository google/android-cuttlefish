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
package com.android.cuttlefish.wifi.tests;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkInfo;
import android.net.ConnectivityManager;
import android.net.wifi.SupplicantState;
import android.net.wifi.WifiConfiguration;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.support.test.InstrumentationRegistry;
import android.util.Log;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

import java.net.Socket;
import java.util.List;

/**
 * Tests used to validate E2E WIFI functionality.
 */
@RunWith(JUnit4.class)
public class WifiE2eTests {
    private static final String TAG = "WifiE2eTests";
    private Context mContext;
    private WifiManager mWifiManager;
    private ConnectivityManager mConnManager;

    @Before
    public void setUp() throws Exception {
        mContext = InstrumentationRegistry.getInstrumentation().getContext();
        mWifiManager = (WifiManager)mContext.getSystemService(Context.WIFI_SERVICE);
        mConnManager = (ConnectivityManager)mContext.getSystemService(Context.CONNECTIVITY_SERVICE);
    }


    private void enableWifi() {
        Log.i(TAG, "Enabling WIFI...");
        mWifiManager.setWifiEnabled(true);
        while (!(mWifiManager.isWifiEnabled() && mWifiManager.pingSupplicant())) {
            Log.i(TAG, "Waiting for WIFI (Enabled: " + mWifiManager.isWifiEnabled() +
                    ", Ready: " + mWifiManager.pingSupplicant() + ")");
            try {
                Thread.sleep(1000);
            } catch (InterruptedException e) {}
        }
    }


    private void disableWifi() {
        Log.i(TAG, "Disabling WIFI...");

        mWifiManager.setWifiEnabled(false);
        while (mWifiManager.isWifiEnabled()) {
            Log.i(TAG, "Waiting for WIFI to be disabled...");
            try {
                Thread.sleep(1000);
            } catch (InterruptedException e) {}
        }
    }


    private void waitForSupplicantState(SupplicantState... expectedStates) {
        while (true) {
            WifiInfo info = mWifiManager.getConnectionInfo();
            SupplicantState currentState = info.getSupplicantState();

            Log.i(TAG, "WIFI State: " + currentState);
            for (SupplicantState state : expectedStates) {
                if (currentState == state) {
                    Log.i(TAG, "WIFI is now in expected state.");
                    return;
                }
            }

            try {
                Thread.sleep(1000);
            } catch (InterruptedException e) {}
        }
    }


    /**
     * Initialize wifi, erase all settings.
     */
    @Test(timeout = 10 * 1000)
    public void testWifiInitialization() {
        enableWifi();

        List<WifiConfiguration> configs = mWifiManager.getConfiguredNetworks();
        Assert.assertNotNull(configs);
        for (WifiConfiguration config : configs) {
            Log.i(TAG, "Removing network " + config.networkId + ": " + config.SSID);
            Assert.assertTrue(mWifiManager.disableNetwork(config.networkId));
            Assert.assertTrue(mWifiManager.removeNetwork(config.networkId));
        }

        waitForSupplicantState(
                SupplicantState.INACTIVE,
                SupplicantState.DISCONNECTED,
                SupplicantState.SCANNING);

        disableWifi();
    }


    /**
     * Verify that WIFI stack is able to get up and connect to network in
     * 60 seconds.
     */
    @Test(timeout = 60 * 1000)
    public void testWifiConnects() throws Exception {
        // 1. Make sure we start with WIFI disabled.
        // It could be, that WIFI is currently disabled anyway.
        // Let's make sure that's the case.
        disableWifi();

        // 2. Wait until stack is up.
        enableWifi();

        // 3. Configure WIFI:
        //    - Add network,
        //    - Enable network,
        //    - Scan for network
        Log.i(TAG, "Configuring WIFI...");
        WifiConfiguration conf = new WifiConfiguration();
        conf.SSID = "\"AndroidWifi\"";
        conf.allowedKeyManagement.set(WifiConfiguration.KeyMgmt.NONE);
        int networkId = mWifiManager.addNetwork(conf);
        Assert.assertTrue(networkId >= 0);
        mWifiManager.enableNetwork(networkId, false);
        mWifiManager.startScan();

        // 4. Wait until connected.
        Log.i(TAG, "Waiting for connectivity...");
        waitForSupplicantState(SupplicantState.COMPLETED);

        // 5. Wait until WIFI is current network.
        while (true) {
            NetworkInfo net = mConnManager.getActiveNetworkInfo();
            if (net != null && net.getType() == ConnectivityManager.TYPE_WIFI) break;

            Log.i(TAG, "Waiting for WIFI to become primary network for DATA.");

            try {
                Thread.sleep(1000);
            } catch (InterruptedException e) {}
        }

        // 6. Bind process to WIFI network. This should allow us to verify network is functional.
        Network net = mConnManager.getActiveNetwork();
        Assert.assertNotNull(net);
        Assert.assertTrue(mConnManager.bindProcessToNetwork(net));

        // 7. Open connection to google.com servers.
        try (Socket s = new Socket("google.com", 80)) {
            Assert.assertTrue(s.isConnected());
        }
    }
}
