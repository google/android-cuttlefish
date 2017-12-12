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
import android.util.Log;

import java.util.ArrayList;

public class ConnectivityChecker extends JobBase {
    private static final String LOG_TAG = "GceConnChecker";

    private final Context mContext;
    private final GceFuture<Boolean> mConnected = new GceFuture<Boolean>("Connectivity");


    public ConnectivityChecker(Context context) {
        super(LOG_TAG);
        mContext = context;
    }


    @Override
    public int execute() {
        ConnectivityManager connManager = (ConnectivityManager) mContext.getSystemService(Context.CONNECTIVITY_SERVICE);
        if (mConnected.isDone()) return 0;

        NetworkInfo[] networks = connManager.getAllNetworkInfo();
        ArrayList<String> connected = new ArrayList<String>();
        ArrayList<String> disconnected = new ArrayList<String>();

        for (NetworkInfo network : networks) {
            if (network.isConnected()) {
                connected.add(network.getTypeName());
                mConnected.set(true);
                break;
            } else {
                disconnected.add(network.getTypeName());
            }
        }

        Log.i(LOG_TAG, "Connectivity status: connected:" + connected + ", disconnected:" + disconnected);

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
