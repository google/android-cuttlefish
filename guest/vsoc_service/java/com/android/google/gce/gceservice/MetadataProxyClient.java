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

import android.net.LocalSocket;
import android.net.LocalSocketAddress;
import android.util.Log;
import java.io.DataInputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.List;

/** Background thread polling MetadataClient for updates.
 *
 * This thread will attach itself to @gce_metadata local socket and
 * poll the gce metadata proxy for changes.
 *
 * Metadata proxy always issues minimum of two messages:
 * - initial metadata,
 * - most recently updated metadata
 * followed by whatever updates it detects.
 *
 * Updates are sent to all clients as:
 * - 32bit integer 'length', describing length of metadata content,
 * - 'length' bytes of JSON file.
 */
public class MetadataProxyClient {
    private final String LOG_TAG = "GceMetadataProxyClient";
    private LocalSocket mSocket = null;
    private DataInputStream mStream = null;

    /* Metadata proxy issues length of metadata update as 4-byte integer
     * in native order.
     */
    private final ByteBuffer mBuffer = ByteBuffer.allocate(4);

    public MetadataProxyClient() {
        mBuffer.order(java.nio.ByteOrder.nativeOrder());
    }

    public String read() throws IOException {
        try {
            if (mSocket == null) {
                mSocket = new LocalSocket();
                mSocket.connect(new LocalSocketAddress("gce_metadata"));
                mStream = new DataInputStream(mSocket.getInputStream());
            }

            if (mSocket.getInputStream().available() == 0)
                return null;

            // We only need 4 bytes for byte buffer, that is, size of uint32_t.
            // Data sent from MetadataProxy is rather simple:
            // - 4 bytes 'length', followed by
            // - 'length' bytes content.
            // All data is in host native byte order, hence ByteBuffer.
            mBuffer.clear();
            mStream.read(mBuffer.array(), 0, 4);
            int length = mBuffer.getInt();

            if ((length < 0) || (length > (1 << 20))) {
                throw new IOException("Invalid metadata length: " + length +
                        ". Restarting client.");
            }

            byte[] contentBytes = new byte[length];
            mStream.read(contentBytes, 0, length);
            String content = new String(contentBytes);
            return content;
        } catch (IOException e) {
            Log.e(LOG_TAG, "Could not talk to metadata proxy", e);
            mSocket = null;
            throw e;
        }
    }
}
