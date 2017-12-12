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

import android.content.ContentResolver;
import android.content.Context;
import android.provider.Settings;
import android.provider.Settings.SettingNotFoundException;
import android.util.Log;

/**
 * Forces pacakge verification to be off on N and N-MR1 by adjusting package_verifier_user_consent.
 *
 * This is needed because AVDs don't have a touch screen, and the consent
 * dialog will block apk installs.
 *
 * Possible values for consent seem to be:
 *   -1 The user refused
 *    0 Ask the user
 *    1 The user accepted
 *
 * This code polls because Android may overwrite a non-zero value with a 0
 * at some point after boot completes. However, this happens only on some
 * boots, so it can't be a blocker for boot complete.
 */
class PackageVerificationConsentEnforcer extends JobBase {
    private static final String LOG_TAG = "GcePVCR";
    private static final String PACKAGE_VERIFIER_USER_CONSENT = "package_verifier_user_consent";
    private final Context mContext;

    // Chosen to avoid the possible values (see top comment).
    private int mLastObservedValue = -2;


    public PackageVerificationConsentEnforcer(Context context) {
        super(LOG_TAG);
        mContext = context;
    }


    public int execute() {
        if (android.os.Build.VERSION.SDK_INT < 24) {
            // Skip older android versions.
            return 0;
        }

        try {
            ContentResolver contentResolver = mContext.getContentResolver();
            int value = Settings.Secure.getInt(contentResolver, PACKAGE_VERIFIER_USER_CONSENT);
            if (value != mLastObservedValue) {
                mLastObservedValue = value;
            }

            if (value == 0) {
                Settings.Secure.putInt(mContext.getContentResolver(), PACKAGE_VERIFIER_USER_CONSENT, -1);
            }
        } catch (SettingNotFoundException e) {
        }

        return 1;
    }


    public void onDependencyFailed(Exception e) {
        Log.e(LOG_TAG, "Could not start Consent Enforcer.", e);
    }
}


