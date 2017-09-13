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

/** Metadata attribute.
 * Metadata attributes are key-value pairs that come from
 * two different sources:
 * - project attributes define (default/fallback) values for all instances
 *   within a GCE project,
 * - instance attributes define (specific) values for individual instance.
 *
 * Instance attributes take precedence over the project attributes.
 * To ensure that we're properly reporting metadata value, we need to
 * remember both project- and instance attribute values, to be able
 * to report project-wide configuration if instance configuration has
 * been reverted.
 */
public class MetadataAttribute {
    private final String mAttributeName;
    private String mInitialProjectValue = null;
    private String mInitialInstanceValue = null;
    private String mProjectValue = null;
    private String mInstanceValue = null;
    /* Keeping information about previously applied value
     * makes it easier to de-duplicate updates sent to listeners.
     */
    private String mAppliedValue = null;

    /** Constructor.
     *
     * @param attributeName specifies name of the attribute.
     */
    public MetadataAttribute(String attributeName) {
        mAttributeName = attributeName;
    }

    /** Set project-wide value as found in initial metadata.
     */
    public void setInitialProjectValue(String value) {
        mInitialProjectValue = value;
        mProjectValue = value;
    }

    /** Set iinstance specific value as found in initial metadata.
     */
    public void setInitialInstanceValue(String value) {
        mInitialInstanceValue = value;
        mInstanceValue = value;
    }

    /** Set project-wide value for this attribute.
     */
    public void setProjectValue(String value) {
        mProjectValue = value;
    }

    /** Set instance specific value for this attribute.
     */
    public void setInstanceValue(String value) {
        mInstanceValue = value;
    }

    /** Mark current value as an applied value.
     */
    public void apply() {
        mAppliedValue = getValue();
    }

    /** Return attribute name.
     */
    public String getName() {
        return mAttributeName;
    }

    /** Return initial value for the attribute.
     * Promotes instance value, if declared.
     *
     * @return initial instance value, if present, otherwise
     *   initial project value.
     */
    public String getInitialValue() {
        if (mInitialInstanceValue != null) {
            return mInitialInstanceValue;
        }
        return mInitialProjectValue;
    }

    /** Return true, if attribute has been modified since last call to apply().
     */
    public boolean isModified() {
        String value = getValue();
        return !(mAppliedValue == null ? value == null : mAppliedValue.equals(value));
    }

    /** Return attribute value.
     *
     * @return instance value, if exists; otherwise returns project value.
     */
    public String getValue() {
        if (mInstanceValue != null) {
            return mInstanceValue;
        }
        return mProjectValue;
    }

    /** Return previously applied value.
     *
     * @see MetadataAttribute.apply()
     */
    public String getPreviousValue() {
        return mAppliedValue;
    }
}
