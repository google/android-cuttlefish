/*
 * Copyright (C) 2023 The Android Open Source Project
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
package com.android.cuttlefish.displayhotplughelper;

import android.app.Activity;
import android.hardware.display.DisplayManager;
import android.os.Bundle;
import android.util.Log;
import android.view.Display;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/**
 * Helper application to print display information to logcat in predictable
 * format for CuttlefishDisplayHotplugTests.
 */
public class DisplayHotplugHelperApp extends Activity {

    private static final String TAG = "DisplayHotplugHelper";

    private static final String HELPER_APP_UUID_FLAG = "display_hotplug_uuid";

    private JSONObject getDisplayInfo(Display display) throws JSONException {
        // Cuttlefish displays only have a single mode using the max resolution.
        final Display.Mode displayMode = display.getMode();

        JSONObject displayInfo = new JSONObject();
        displayInfo.put("id", display.getDisplayId());
        displayInfo.put("width", displayMode.getPhysicalWidth());
        displayInfo.put("height", displayMode.getPhysicalHeight());
        return displayInfo;
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        String loggingIdentifier = getIntent().getExtras().getString(HELPER_APP_UUID_FLAG);

        Display[] displays = getSystemService(DisplayManager.class).getDisplays();
        try {
            JSONArray displayInfos = new JSONArray();
            for (Display display : displays) {
                displayInfos.put(getDisplayInfo(display));
            }
            JSONObject displayInfo = new JSONObject();
            displayInfo.put("displays", displayInfos);

            Log.e(TAG, loggingIdentifier + " displays: " + displayInfo);
        } catch (JSONException e) {
            Log.e(TAG, "Failed to create display info JSON: " + e);
        }

        finishAndRemoveTask();
    }
}
