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
package com.android.cuttlefish.ril.tests;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkInfo;
import android.net.wifi.SupplicantState;
import android.net.wifi.WifiConfiguration;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.support.test.InstrumentationRegistry;
import android.telephony.CellInfoGsm;
import android.telephony.CellSignalStrengthGsm;
import android.telephony.TelephonyManager;
import android.util.Log;

import static org.hamcrest.Matchers.greaterThan;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

import java.net.Socket;
import java.util.List;

/**
 * Tests used to validate E2E WIFI functionality.
 */
@RunWith(JUnit4.class)
public class RilE2eTests {
    private static final String TAG = "RilE2eTests";
    private Context mContext;
    private WifiManager mWifiManager;
    private ConnectivityManager mConnManager;
    private TelephonyManager mTeleManager;

    @Before
    public void setUp() throws Exception {
        mContext = InstrumentationRegistry.getInstrumentation().getContext();
        mWifiManager = (WifiManager)mContext.getSystemService(Context.WIFI_SERVICE);
        mConnManager = (ConnectivityManager)mContext.getSystemService(Context.CONNECTIVITY_SERVICE);
        mTeleManager = (TelephonyManager)mContext.getSystemService(Context.TELEPHONY_SERVICE);
        disableAllWifiNetworks();
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


    private void disableAllWifiNetworks() {
        enableWifi();

        List<WifiConfiguration> configs = mWifiManager.getConfiguredNetworks();
        Assert.assertNotNull(configs);
        for (WifiConfiguration config : configs) {
            Log.i(TAG, "Removing network " + config.networkId + ": " + config.SSID);
            Assert.assertTrue(mWifiManager.disableNetwork(config.networkId));
            Assert.assertTrue(mWifiManager.removeNetwork(config.networkId));
        }

        disableWifi();
    }


    /**
     * Verify that WIFI stack is able to get up and connect to network in
     * 60 seconds.
     */
    @Test(timeout = 60 * 1000)
    public void testRilConnects() throws Exception {
        while (true) {
            NetworkInfo net = mConnManager.getActiveNetworkInfo();
            if (net != null && net.getType() == ConnectivityManager.TYPE_MOBILE) break;

            Log.i(TAG, "Waiting for MOBILE to become primary network for DATA.");

            try {
                Thread.sleep(1000);
            } catch (InterruptedException e) {}
        }

        // Bind process to MOBILE network. This should allow us to verify network is functional.
        Network net = mConnManager.getActiveNetwork();
        Assert.assertNotNull(net);
        Assert.assertTrue(mConnManager.bindProcessToNetwork(net));

        // Open connection to google.com servers.
        try (Socket s = new Socket("google.com", 80)) {
            Assert.assertTrue(s.isConnected());
        }
    }


    /**
     * Verify that AVD is connected to our virtual network operator and is
     * phone-, sms- and data capable.
     */
    @Test
    public void testBasicPhoneAttributes() throws Exception {
        Assert.assertEquals("Android Virtual Operator", mTeleManager.getNetworkOperatorName());
        Assert.assertFalse(mTeleManager.isNetworkRoaming());
        Assert.assertTrue(mTeleManager.isSmsCapable());
        Assert.assertSame(TelephonyManager.NETWORK_TYPE_LTE, mTeleManager.getVoiceNetworkType());
        Assert.assertSame(TelephonyManager.SIM_STATE_READY, mTeleManager.getSimState());
        Assert.assertSame(TelephonyManager.PHONE_TYPE_GSM, mTeleManager.getPhoneType());
        Assert.assertSame(mTeleManager.getPhoneCount(), 1);
        // See SIM FS response for 178 28480 (Cuttlefish RIL).
        Assert.assertEquals("+15551234567", mTeleManager.getLine1Number());
        // See SIM FS response for 178 28615 (Cuttlefish RIL).
        Assert.assertEquals("+15557654321", mTeleManager.getVoiceMailNumber());
        Assert.assertSame(TelephonyManager.DATA_CONNECTED, mTeleManager.getDataState());
    }

    // See b/74256305
    @Ignore
    @Test
    public void testSignalLevels() throws Exception {
        CellInfoGsm cellinfogsm = (CellInfoGsm)mTeleManager.getAllCellInfo().get(0);
        CellSignalStrengthGsm cellSignalStrengthGsm = cellinfogsm.getCellSignalStrength();
        int bars = cellSignalStrengthGsm.getLevel();
        Assert.assertThat("Signal Bars", bars, greaterThan(1));
    }
}
