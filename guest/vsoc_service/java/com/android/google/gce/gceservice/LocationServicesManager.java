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
import android.content.Intent;
import android.os.Build;
import android.util.Log;
import com.android.google.gce.gceservice.GceFuture;
import com.android.google.gce.gceservice.JobBase;

/**
 * Configure Location Services on Android Jellybean.
 * No action on more recent versions of Android.
 */
class LocationServicesManager extends JobBase {
    private static final String LOG_TAG = "GceLocationServicesManager";
    private static final String ACTION_LOCATION_SERVICES_CONSENT_INTENT =
            "com.google.android.gsf.action.SET_USE_LOCATION_FOR_SERVICES";
    private static final String EXTRA_LOCATION_SERVICES_CONSENT_DISABLE =
            "disable";
    private final Context mContext;
    private final GceFuture<Boolean> mResult = new GceFuture<Boolean>("Location Services");


    LocationServicesManager(Context context) {
        super(LOG_TAG);
        mContext = context;
    }


    public int execute() {
        /* Check if we're running Jellybean.
         * Sadly, we can't use version name Build.VERSION_CODES.JELLY_BEAN_MR2
         * because MR1 and MR0 don't know this number.
         */
        if (Build.VERSION.SDK_INT <= 18) {
            Intent intent = new Intent();
            intent.setAction(ACTION_LOCATION_SERVICES_CONSENT_INTENT);
            intent.setFlags(intent.getFlags() |
                    Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
            intent.putExtra(EXTRA_LOCATION_SERVICES_CONSENT_DISABLE, false);
            mContext.startActivity(intent);
        }

        mResult.set(true);
        return 0;
    }


    public void onDependencyFailed(Exception e) {
        Log.e(LOG_TAG, "Could not configure LocationServices.", e);
        mResult.set(e);
    }


    public GceFuture<Boolean> getLocationServicesReady() {
        return mResult;
    }
}
