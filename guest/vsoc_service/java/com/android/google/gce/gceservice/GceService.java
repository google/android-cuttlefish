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

import android.app.Service;
import android.bluetooth.BluetoothAdapter;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.ConnectivityManager;
import android.util.Log;
import android.os.Binder;
import android.os.IBinder;
import android.os.ServiceManager;
import java.io.FileDescriptor;
import java.io.PrintWriter;
import java.util.List;

/**
 * Service is started by the BootCompletedReceiver at the end of Android Boot process.
 * Responsible for final configuration changes and emitting final BOOT_COMPLETED message.
 */
public class GceService extends Service {
    private static final String LOG_TAG = "GceService";
    /* Intent sent by the BootCompletedReceiver upon receiving ACTION_BOOT_COMPLETED broadcast. */
    public static final String INTENT_ACTION_CONFIGURE = "com.android.google.gce.gceservice.CONFIGURE";
    public static final String INTENT_ACTION_NETWORK_CHANGED = "com.android.google.gce.gceservice.NETWORK_CHANGED";
    public static final String INTENT_ACTION_BLUETOOTH_CHANGED = "com.android.google.gce.gceservice.BLUETOOTH_CHANGED";
    private static final int NETWORK_OR_BOOT_TIMEOUT = 30;

    private final JobExecutor mExecutor = new JobExecutor();
    private final ConnectivityChecker mConnChecker = new ConnectivityChecker(this);
    private final LocationServicesManager mLocationServices = new LocationServicesManager(this);
    private final PackageVerifierManager mPackageVerifier = new PackageVerifierManager(this);
    private final PackageVerificationConsentEnforcer mConsentEnforcer = new PackageVerificationConsentEnforcer(this);
    private final BootReporter mBootReporter = new BootReporter();
    private final GceBroadcastReceiver mBroadcastReceiver = new GceBroadcastReceiver();
    private final BluetoothChecker mBluetoothChecker = new BluetoothChecker();
    private final TombstoneChecker mTombstoneChecker = new TombstoneChecker();

    private GceWifiManager mWifiManager = null;
    private String mMostRecentAction = null;
    private BinderService mBinderService;


    public GceService() {}


    @Override
    public void onCreate() {
        try {
            super.onCreate();
            mBootReporter.reportBootStarted();
            registerBroadcastReceivers();

            mWifiManager = new GceWifiManager(this, mExecutor);

            mExecutor.schedule(mLocationServices);
            mExecutor.schedule(mPackageVerifier);
            mExecutor.schedule(mConsentEnforcer);
            mExecutor.schedule(mWifiManager);
            mExecutor.schedule(mBluetoothChecker);
            // TODO(ender): TombstoneChecker is disabled, because we no longer have the code that
            // produces /ts_snap.txt file. We need to rethink how TombstoneChecker should work.
            // mExecutor.schedule(mTombstoneChecker);

            mExecutor.schedule(mBootReporter,
                    mLocationServices.getLocationServicesReady(),
                    mPackageVerifier.getPackageVerifierReady(),
                    mConnChecker.getConnected(),
                    mWifiManager.getInitialWifiStateChangeReady(),
                    mBluetoothChecker.getEnabled()
                    // mTombstoneChecker.getTombstoneResult()
                    );
        } catch (Exception e) {
            Log.e(LOG_TAG, "Exception caught", e);
        }
    }


    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }


    /** Register broadcast listeners.
     *
     * Certain intents can no longer be used to start a service or activity, but
     * can still be registered for, such as CONNECTIVITY_ACTION (Android N).
     */
    private void registerBroadcastReceivers() {
        IntentFilter filter = new IntentFilter();
        filter.addAction(ConnectivityManager.CONNECTIVITY_ACTION);
        filter.addAction(BluetoothAdapter.ACTION_STATE_CHANGED);
        this.registerReceiver(mBroadcastReceiver, filter);
    }


    /** StartService entry point.
     */
    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null) {
            mMostRecentAction = intent.getAction();
        } else {
            Log.w(LOG_TAG, "Previous execution failed. Retrying.");
        }

        if (mMostRecentAction == null) {
            Log.e(LOG_TAG, "Missing intent action.");
        }

        if (INTENT_ACTION_CONFIGURE.equals(mMostRecentAction)) {
            mExecutor.schedule(mConnChecker);
        } else if (INTENT_ACTION_NETWORK_CHANGED.equals(mMostRecentAction)) {
            mExecutor.schedule(mConnChecker);
        } else if (INTENT_ACTION_BLUETOOTH_CHANGED.equals(mMostRecentAction)) {
            mExecutor.schedule(mBluetoothChecker);
        }

        mBinderService = new BinderService();
        ServiceManager.addService("gce", mBinderService, false);

        /* If anything goes wrong, make sure we receive intent again. */
        return Service.START_STICKY;
    }

    /** Dump the virtual device state
     */
    private void dumpInternal(FileDescriptor fd, PrintWriter pw, String[] args) {
        pw.println("Boot reporter:");
        List<String> messageList = mBootReporter.getMessageList();
        for (int i = 0; i < messageList.size(); i++) {
            pw.println("  " + messageList.get(i));
        }
        pw.println("");
        pw.println("Current system service state:");
        pw.println("  Location service ready: "
            + mLocationServices.getLocationServicesReady().isDone());
        pw.println("  Package verifier ready: "
            + mPackageVerifier.getPackageVerifierReady().isDone());
        pw.println("  Network connected: " + mConnChecker.getConnected().isDone());
        pw.println("  WiFi configured: " + mWifiManager.getInitialWifiStateChangeReady().isDone());
        pw.println("  Bluetooth enabled: " + mBluetoothChecker.getEnabled().isDone());
        pw.println("  Tombstone dropped (on boot): "
            + !mTombstoneChecker.getTombstoneResult().isDone());
        pw.println("");
    }

    private final class BinderService extends Binder {
        @Override protected void dump(FileDescriptor fd, PrintWriter pw, String[] args) {
            dumpInternal(fd, pw, args);
        }
    }
}
