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
import android.net.wifi.SupplicantState;
import android.net.wifi.WifiConfiguration;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.util.Log;

import androidx.test.InstrumentationRegistry;
import androidx.test.core.app.ApplicationProvider;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketTimeoutException;
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
        mContext = ApplicationProvider.getApplicationContext();
        mWifiManager = (WifiManager)mContext.getSystemService(Context.WIFI_SERVICE);
        mConnManager = (ConnectivityManager)mContext.getSystemService(Context.CONNECTIVITY_SERVICE);
    }


    @SuppressWarnings("unused")
    private void enableWifi() throws InterruptedException {
        Log.i(TAG, "Enabling WIFI...");
        mWifiManager.setWifiEnabled(true);
        while (!mWifiManager.isWifiEnabled()) {
            Log.i(TAG, "Waiting for WIFI to be enabled...");
            Thread.sleep(1000);
        }
    }


    @SuppressWarnings("unused")
    private void disableWifi() throws InterruptedException {
        Log.i(TAG, "Disabling WIFI...");

        mWifiManager.setWifiEnabled(false);
        while (mWifiManager.isWifiEnabled()) {
            Log.i(TAG, "Waiting for WIFI to be disabled...");
            Thread.sleep(1000);
        }
    }


    private void waitForSupplicantState(SupplicantState... expectedStates)
            throws InterruptedException {
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

            Thread.sleep(1000);
        }
    }


    private void enableNetwork(String SSID) {
        WifiConfiguration conf = new WifiConfiguration();
        conf.SSID = SSID;
        conf.allowedKeyManagement.set(WifiConfiguration.KeyMgmt.NONE);
        int networkId = mWifiManager.addNetwork(conf);
        Assert.assertTrue(networkId >= 0);
        mWifiManager.enableNetwork(networkId, false);
    }


    /**
     * Initialize wifi, erase all settings.
     */
    @Test(timeout = 10 * 1000)
    public void testWifiInitialization() throws Exception {
        enableWifi();

        List<WifiConfiguration> configs = mWifiManager.getConfiguredNetworks();
        Assert.assertNotNull(configs);
        for (WifiConfiguration config : configs) {
            Log.i(TAG, "Removing network " + config.networkId + ": " + config.SSID);
            mWifiManager.disableNetwork(config.networkId);
            mWifiManager.removeNetwork(config.networkId);
        }
        configs = mWifiManager.getConfiguredNetworks();
        Assert.assertEquals(0, configs.size());

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
        enableNetwork("\"VirtWifi\"");
        enableNetwork("\"AndroidWifi\"");
        mWifiManager.startScan();

        // 4. Wait until connected.
        Log.i(TAG, "Waiting for connectivity...");
        waitForSupplicantState(SupplicantState.COMPLETED);

        // 5. Wait until WIFI is current network.
        while (true) {
            NetworkInfo net = mConnManager.getActiveNetworkInfo();
            if (net != null && net.getType() == ConnectivityManager.TYPE_WIFI) break;

            Log.i(TAG, "Waiting for WIFI to become primary network for DATA.");

            Thread.sleep(1000);
        }

        // 6. Bind process to WIFI network. This should allow us to verify network is functional.
        Network net = mConnManager.getActiveNetwork();
        Assert.assertNotNull(net);
        Assert.assertTrue(mConnManager.bindProcessToNetwork(net));

        // 7. Open connection to Google public DNS server
        InetSocketAddress addr = new InetSocketAddress("8.8.8.8", 53);
        while (true) {
            try (Socket s = new Socket()) {
                Log.d(TAG, "Testing socket connection to 8.8.8.8:53...");
                s.connect(addr, 5000); // use a socket connection timeout of 5s
                Assert.assertTrue(
                        "Failed to make socket connection to 8.8.8.8:53", s.isConnected());
                return;
            } catch (SocketTimeoutException e) {
                Log.d(TAG, "Socket connection to 8.8.8.8:53 timed out (5s), retry...");
            }
        }
    }
}
