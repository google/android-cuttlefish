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

import static org.hamcrest.Matchers.greaterThan;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkInfo;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.telephony.CellInfoLte;
import android.telephony.CellSignalStrengthLte;
import android.telephony.TelephonyManager;
import android.util.Log;

import androidx.test.InstrumentationRegistry;

import com.android.compatibility.common.util.PropertyUtil;

import org.junit.Assert;
import org.junit.Assume;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

import java.net.Socket;

/**
 * Tests used to validate E2E RIL functionality.
 */
@RunWith(JUnit4.class)
public class RilE2eTests {
    private static final String TAG = "RilE2eTests";
    private static final int MAX_POLL_DISABLED_WIFI_COUNT = 10;
    private Context mContext;
    private WifiManager mWifiManager;
    private ConnectivityManager mConnManager;
    private TelephonyManager mTeleManager;

    @Before
    public void setUp() throws Exception {
        // Ideally this should be done in the @BeforeClass hook, but that would
        // make tradefed unhappy with a bunch "test did not run due to
        // instrumentation issue. See run level error for reason." errors.
        Assume.assumeFalse(
                "Skip testing deprecated radio HAL from Q or earlier vendor",
                PropertyUtil.getFirstApiLevel() <= Build.VERSION_CODES.Q);

        mContext = InstrumentationRegistry.getInstrumentation().getContext();
        mWifiManager = (WifiManager)mContext.getSystemService(Context.WIFI_SERVICE);
        mConnManager = (ConnectivityManager)mContext.getSystemService(Context.CONNECTIVITY_SERVICE);
        mTeleManager = (TelephonyManager)mContext.getSystemService(Context.TELEPHONY_SERVICE);
        // There must not be an active wifi connection while running the test or else
        // getActiveNetworkInfo() will return that instead of the telephony network.
        // Turning wifi off should do the trick.
        disableWifi();
    }


    private void disableWifi() throws Exception {
        Log.i(TAG, "Disabling WIFI...");

        mWifiManager.setWifiEnabled(false);
        int count = MAX_POLL_DISABLED_WIFI_COUNT;
        while (mWifiManager.isWifiEnabled() && count-- > 0) {
            Log.i(TAG, "Waiting for WIFI to be disabled...");
            try {
                Thread.sleep(1000);
            } catch (InterruptedException e) {}
        }
        if (count < 0) {
            Log.e(TAG, "Reached max number of polls while waiting to disable wifi");
            throw new Exception("Timed out waiting for wifi to be disabled");
        }
    }


    /**
     * Verify that RIL stack is able to get up and connect to network in
     * 20 seconds.
     */
    @Test(timeout = 20 * 1000)
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

    @Test
    public void testSignalLevels() throws Exception {
        CellInfoLte cellInfo = (CellInfoLte) mTeleManager.getAllCellInfo().get(0);
        CellSignalStrengthLte signalStrength = cellInfo.getCellSignalStrength();
        int bars = signalStrength.getLevel();
        Assert.assertThat("Signal Bars", bars, greaterThan(1));
    }
}
