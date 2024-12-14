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

package com.google.android.telephony.satellite;

import android.annotation.NonNull;
import android.content.Intent;
import android.os.Binder;
import android.os.IBinder;
import android.telephony.IBooleanConsumer;
import android.telephony.IIntegerConsumer;
import android.telephony.satellite.stub.ISatelliteCapabilitiesConsumer;
import android.telephony.satellite.stub.ISatelliteListener;
import android.telephony.satellite.stub.NTRadioTechnology;
import android.telephony.satellite.stub.SatelliteCapabilities;
import android.telephony.satellite.stub.SatelliteDatagram;
import android.telephony.satellite.stub.SatelliteImplBase;
import android.telephony.satellite.stub.SatelliteModemState;
import android.telephony.satellite.stub.SatelliteResult;
import android.telephony.satellite.stub.SatelliteService;
import android.telephony.satellite.stub.SatelliteModemEnableRequestAttributes;
import android.telephony.satellite.stub.SystemSelectionSpecifier;

import com.android.internal.util.FunctionalUtils;
import com.android.telephony.Rlog;

import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.Executor;

public class CFSatelliteService extends SatelliteImplBase {
    private static final String TAG = "CFSatelliteService";

    // Hardcoded values below
    private static final int SATELLITE_ALWAYS_VISIBLE = 0;
    /** SatelliteCapabilities constant indicating that the radio technology is proprietary. */
    private static final int[] SUPPORTED_RADIO_TECHNOLOGIES =
            new int[] {NTRadioTechnology.PROPRIETARY};
    /** SatelliteCapabilities constant indicating that pointing to satellite is required. */
    private static final boolean POINTING_TO_SATELLITE_REQUIRED = true;
    /** SatelliteCapabilities constant indicating the maximum number of characters per datagram. */
    private static final int MAX_BYTES_PER_DATAGRAM = 339;

    @NonNull private final Set<ISatelliteListener> mListeners = new HashSet<>();

    private boolean mIsCommunicationAllowedInLocation;
    private boolean mIsEnabled;
    private boolean mIsSupported;
    private int mModemState;
    private boolean mIsEmergnecy;

    /**
     * Create CFSatelliteService using the Executor specified for methods being called from
     * the framework.
     *
     * @param executor The executor for the framework to use when executing satellite methods.
     */
    public CFSatelliteService(@NonNull Executor executor) {
        super(executor);
        mIsCommunicationAllowedInLocation = true;
        mIsEnabled = false;
        mIsSupported = true;
        mModemState = SatelliteModemState.SATELLITE_MODEM_STATE_OFF;
        mIsEmergnecy = false;
    }

    /**
     * Zero-argument constructor to prevent service binding exception.
     */
    public CFSatelliteService() {
        this(Runnable::run);
    }

    @Override
    public IBinder onBind(Intent intent) {
        if (SatelliteService.SERVICE_INTERFACE.equals(intent.getAction())) {
            logd("CFSatelliteService bound");
            return new CFSatelliteService().getBinder();
        }
        return null;
    }

    @Override
    public void onCreate() {
        super.onCreate();
        logd("onCreate");
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        logd("onDestroy");
    }

    @Override
    public void setSatelliteListener(@NonNull ISatelliteListener listener) {
        logd("setSatelliteListener");
        mListeners.add(listener);
    }

    @Override
    public void requestSatelliteListeningEnabled(boolean enable, int timeout,
            @NonNull IIntegerConsumer errorCallback) {
        logd("requestSatelliteListeningEnabled");
        if (!verifySatelliteModemState(errorCallback)) {
            return;
        }
        if (enable) {
            updateSatelliteModemState(SatelliteModemState.SATELLITE_MODEM_STATE_LISTENING);
        } else {
            updateSatelliteModemState(SatelliteModemState.SATELLITE_MODEM_STATE_IDLE);
        }
        runWithExecutor(() -> errorCallback.accept(SatelliteResult.SATELLITE_RESULT_SUCCESS));
    }

    @Override
    public void requestSatelliteEnabled(SatelliteModemEnableRequestAttributes enableAttributes,
            @NonNull IIntegerConsumer errorCallback) {
        logd("requestSatelliteEnabled");
        if (enableAttributes.isEnabled) {
            enableSatellite(errorCallback);
        } else {
            disableSatellite(errorCallback);
        }
        mIsEmergnecy = enableAttributes.isEmergencyMode;
    }

    private void enableSatellite(@NonNull IIntegerConsumer errorCallback) {
        mIsEnabled = true;
        updateSatelliteModemState(SatelliteModemState.SATELLITE_MODEM_STATE_IDLE);
        runWithExecutor(() -> errorCallback.accept(SatelliteResult.SATELLITE_RESULT_SUCCESS));
    }

    private void disableSatellite(@NonNull IIntegerConsumer errorCallback) {
        mIsEnabled = false;
        updateSatelliteModemState(SatelliteModemState.SATELLITE_MODEM_STATE_OFF);
        runWithExecutor(() -> errorCallback.accept(SatelliteResult.SATELLITE_RESULT_SUCCESS));
    }

    @Override
    public void requestIsSatelliteEnabled(@NonNull IIntegerConsumer errorCallback,
            @NonNull IBooleanConsumer callback) {
        logd("requestIsSatelliteEnabled");
        runWithExecutor(() -> callback.accept(mIsEnabled));
    }

    @Override
    public void requestIsSatelliteSupported(@NonNull IIntegerConsumer errorCallback,
            @NonNull IBooleanConsumer callback) {
        logd("requestIsSatelliteSupported");
        runWithExecutor(() -> callback.accept(mIsSupported));
    }

    @Override
    public void requestSatelliteCapabilities(@NonNull IIntegerConsumer errorCallback,
            @NonNull ISatelliteCapabilitiesConsumer callback) {
        logd("requestSatelliteCapabilities");
        SatelliteCapabilities capabilities = new SatelliteCapabilities();
        capabilities.supportedRadioTechnologies = SUPPORTED_RADIO_TECHNOLOGIES;
        capabilities.isPointingRequired = POINTING_TO_SATELLITE_REQUIRED;
        capabilities.maxBytesPerOutgoingDatagram = MAX_BYTES_PER_DATAGRAM;
        runWithExecutor(() -> callback.accept(capabilities));
    }

    @Override
    public void startSendingSatellitePointingInfo(@NonNull IIntegerConsumer errorCallback) {
        logd("startSendingSatellitePointingInfo");
        if (!verifySatelliteModemState(errorCallback)) {
            return;
        }
        runWithExecutor(() -> errorCallback.accept(SatelliteResult.SATELLITE_RESULT_SUCCESS));
    }

    @Override
    public void stopSendingSatellitePointingInfo(@NonNull IIntegerConsumer errorCallback) {
        logd("stopSendingSatellitePointingInfo");
        runWithExecutor(() -> errorCallback.accept(SatelliteResult.SATELLITE_RESULT_SUCCESS));
    }

    @Override
    public void pollPendingSatelliteDatagrams(@NonNull IIntegerConsumer errorCallback) {
        logd("pollPendingSatelliteDatagrams");
        runWithExecutor(() -> errorCallback.accept(SatelliteResult.SATELLITE_RESULT_SUCCESS));
    }

    @Override
    public void sendSatelliteDatagram(@NonNull SatelliteDatagram datagram, boolean isEmergency,
            @NonNull IIntegerConsumer errorCallback) {
        logd("sendSatelliteDatagram");
        runWithExecutor(() -> errorCallback.accept(SatelliteResult.SATELLITE_RESULT_SUCCESS));
    }

    @Override
    public void requestSatelliteModemState(@NonNull IIntegerConsumer errorCallback,
            @NonNull IIntegerConsumer callback) {
        logd("requestSatelliteModemState");
        runWithExecutor(() -> callback.accept(mModemState));
    }

    @Override
    public void requestTimeForNextSatelliteVisibility(@NonNull IIntegerConsumer errorCallback,
            @NonNull IIntegerConsumer callback) {
        logd("requestTimeForNextSatelliteVisibility");
        runWithExecutor(() -> callback.accept(SATELLITE_ALWAYS_VISIBLE));
    }

    @Override
    public void updateSatelliteSubscription(@NonNull String iccId,
            @NonNull IIntegerConsumer resultCallback) {
        logd("updateSatelliteSubscription: iccId=" + iccId);
        runWithExecutor(() -> resultCallback.accept(SatelliteResult.SATELLITE_RESULT_SUCCESS));
    }

    @Override
    public void updateSystemSelectionChannels(
            @NonNull List<SystemSelectionSpecifier> systemSelectionSpecifiers,
            @NonNull IIntegerConsumer resultCallback) {
        logd(" updateSystemSelectionChannels: "
                        + "systemSelectionSpecifiers=" + systemSelectionSpecifiers);
        runWithExecutor(() -> resultCallback.accept(SatelliteResult.SATELLITE_RESULT_SUCCESS));
    }

    /**
     * Helper method to verify that the satellite modem is properly configured to receive requests.
     *
     * @param errorCallback The callback to notify of any errors preventing satellite requests.
     * @return {@code true} if the satellite modem is configured to receive requests and
     *         {@code false} if it is not.
     */
    private boolean verifySatelliteModemState(@NonNull IIntegerConsumer errorCallback) {
        if (!mIsSupported) {
            runWithExecutor(() -> errorCallback.accept(
                SatelliteResult.SATELLITE_RESULT_REQUEST_NOT_SUPPORTED));
            return false;
        }
        if (!mIsEnabled) {
            runWithExecutor(() -> errorCallback.accept(
                SatelliteResult.SATELLITE_RESULT_INVALID_MODEM_STATE));
            return false;
        }
        return true;
    }

    /**
     * Update the satellite modem state and notify listeners if it changed.
     *
     * @param modemState The {@link SatelliteModemState} to update.
     */
    private void updateSatelliteModemState(int modemState) {
        if (modemState == mModemState) {
            return;
        }
        mListeners.forEach(listener -> runWithExecutor(() ->
                listener.onSatelliteModemStateChanged(modemState)));
        mModemState = modemState;
    }

    /**
     * Get the emergency mode or not
     */
    public boolean getIsEmergency() {
        logd("getIsEmergency");
        return mIsEmergnecy;
    }

    /**
     * Execute the given runnable using the executor that this service was created with.
     *
     * @param r A runnable that can throw an exception.
     */
    private void runWithExecutor(@NonNull FunctionalUtils.ThrowingRunnable r) {
        mExecutor.execute(() -> Binder.withCleanCallingIdentity(r));
    }

    /**
     * Log the message to the radio buffer with {@code DEBUG} priority.
     *
     * @param log The message to log.
     */
    private static void logd(@NonNull String log) {
        Rlog.d(TAG, log);
    }
}
