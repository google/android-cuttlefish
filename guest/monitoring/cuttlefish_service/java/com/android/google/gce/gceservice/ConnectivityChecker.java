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
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkInfo;
import android.util.Log;

import java.util.ArrayList;

public class ConnectivityChecker extends JobBase {
    private static final String LOG_TAG = "GceConnChecker";
    private static final String MOBILE_NETWORK_CONNECTED_MESSAGE =
        "VIRTUAL_DEVICE_NETWORK_MOBILE_CONNECTED";

    private final Context mContext;
    private final BootReporter mBootReporter;
    private final GceFuture<Boolean> mConnected = new GceFuture<Boolean>("Connectivity");
    // TODO(schuffelen): Figure out why this has to be static in order to not report 3 times.
    private static boolean reportedMobileConnectivity = false;

    public ConnectivityChecker(Context context, BootReporter bootReporter) {
        super(LOG_TAG);
        mContext = context;
        mBootReporter = bootReporter;
    }


    @Override
    public int execute() {
        ConnectivityManager connManager = (ConnectivityManager) mContext.getSystemService(Context.CONNECTIVITY_SERVICE);
        if (mConnected.isDone()) {
            return 0;
        }

        Network[] networks = connManager.getAllNetworks();
        for (Network network : networks) {
            NetworkInfo info = connManager.getNetworkInfo(network);
            if (info.isConnected()) {
                NetworkCapabilities capabilities = connManager.getNetworkCapabilities(network);
                if (capabilities != null
                        && capabilities.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR)
                        && !reportedMobileConnectivity) {
                    mBootReporter.reportMessage(MOBILE_NETWORK_CONNECTED_MESSAGE);
                    reportedMobileConnectivity = true;
                }
            }
        }

        return 0;
    }


    @Override
    public void onDependencyFailed(Exception e) {
        mConnected.set(e);
    }


    public GceFuture<Boolean> getConnected() {
        return mConnected;
    }
}
