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

import android.content.ContentResolver;
import android.content.Context;
import android.provider.Settings;
import android.provider.Settings.SettingNotFoundException;
import android.util.Log;

/**
 * Disable package verifier.
 */
public class PackageVerifierManager extends JobBase {
    private static final String LOG_TAG = "GcePackageVerifierManager";
    private static final String SETTING_PACKAGE_VERIFIER_ENABLE = "verifier_verify_adb_installs";
    private final Context mContext;
    private final GceFuture<Boolean> mResult =
            new GceFuture<Boolean>("Package Verifier");


    PackageVerifierManager(Context context) {
        super(LOG_TAG);
        mContext = context;
    }


    private boolean getAndLogPackageVerifierState() {
        int package_verifier_state = 1;
        try {
            ContentResolver contentResolver = mContext.getContentResolver();
            package_verifier_state = Settings.Secure.getInt(contentResolver, SETTING_PACKAGE_VERIFIER_ENABLE);
        } catch (SettingNotFoundException e) {
            Log.w(LOG_TAG, "Could not read package verifier state. Assuming it's enabled.");
        }

        return package_verifier_state != 0;
    }


    public int execute() {
        if (getAndLogPackageVerifierState()) {
            Settings.Secure.putInt(mContext.getContentResolver(), SETTING_PACKAGE_VERIFIER_ENABLE, 0);
            // One more call, just to log the state.
            getAndLogPackageVerifierState();
        }

        mResult.set(true);
        return 0;
    }


    public void onDependencyFailed(Exception e) {
        Log.e(LOG_TAG, "Could not disable Package Verifier.", e);
        mResult.set(e);
    }


    public GceFuture<Boolean> getPackageVerifierReady() {
        return mResult;
    }
}
