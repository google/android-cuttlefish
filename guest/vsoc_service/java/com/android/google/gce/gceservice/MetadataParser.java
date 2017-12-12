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

import android.util.Log;
import com.android.google.gce.gceservice.Metadata;
import com.android.google.gce.gceservice.MetadataAttribute;
import java.util.Iterator;
import org.json.JSONException;
import org.json.JSONObject;
import org.json.JSONTokener;

public class MetadataParser {
    private static final String LOG_TAG = "GceMetadataParser";
    /* JSON attributes. */
    private static final String METADATA_JSON_TAG_INSTANCE = "instance";
    private static final String METADATA_JSON_TAG_PROJECT = "project";
    private static final String METADATA_JSON_TAG_ATTRIBUTES = "attributes";

    /** Parse JSON file and extract instance- and project attributes.
     */
    public static boolean parseFromJson(
        String metadataContent, Metadata metadata, boolean isInitialMetadata) {
        /* If there's multiple concatenated JSON objects returned, we only
         * process the first one.
         * If we can't tokenize the response, log an error and stop.
         */
        if (metadataContent == null || metadataContent.isEmpty()) {
            Log.e(LOG_TAG, "No metadata to parse.");
            return false;
        }

        /* Metadata server does not send us deltas, but instead, the whole
         * content each time.
         * Clear all attributes fetched from metadata server first.
         */
        for (MetadataAttribute attribute : metadata.getAttributes()) {
            attribute.setProjectValue(null);
            attribute.setInstanceValue(null);
        }

        JSONObject jsonObject = null;
        try {
            JSONTokener tokener = new JSONTokener(metadataContent);
            jsonObject = (JSONObject)tokener.nextValue();
        } catch (ClassCastException e) {
            Log.w(LOG_TAG, "Likely malformed metadata from server: " + metadataContent);
            return false;
        } catch (JSONException e) {
            Log.e(LOG_TAG, "Could not tokenize JSON returned by metadata server", e);
            return false;
        }

        /* Process project.attributes section of the JSON file. */
        try {
            JSONObject object = jsonObject
                    .getJSONObject(METADATA_JSON_TAG_PROJECT)
                    .getJSONObject(METADATA_JSON_TAG_ATTRIBUTES);
            Iterator<String> keys = object.keys();
            while (keys.hasNext()) {
                String key = keys.next();
                if (isInitialMetadata) {
                    metadata.getAttribute(key).setInitialProjectValue(object.getString(key));
                } else {
                    metadata.getAttribute(key).setProjectValue(object.getString(key));
                }
            }
        } catch (JSONException e) {
            /* This can be perfectly normal, if update does not carry new
             * project attributes.
             */
        }

        /* Process instance.attributes section of the JSON file. */
        try {
            JSONObject object = jsonObject
                    .getJSONObject(METADATA_JSON_TAG_INSTANCE)
                    .getJSONObject(METADATA_JSON_TAG_ATTRIBUTES);
            Iterator<String> keys = object.keys();
            while (keys.hasNext()) {
                String key = keys.next();
                if (isInitialMetadata) {
                    metadata.getAttribute(key).setInitialInstanceValue(object.getString(key));
                } else {
                    metadata.getAttribute(key).setInstanceValue(object.getString(key));
                }
            }
        } catch (JSONException e) {
            /* This can be perfectly normal, if update does not carry new
             * instance attributes.
             */
        }

        return true;
    }
}
