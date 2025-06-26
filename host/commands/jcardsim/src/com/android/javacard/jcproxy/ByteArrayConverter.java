/*
 * Copyright (C) 2024 The Android Open Source Project
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
package com.android.javacard.jcproxy;

/** Utility class for converting between byte arrays and Strings. */
public final class ByteArrayConverter {

    private static final String EMPTY_STRING = "";

    private ByteArrayConverter() {}

    /**
     * Returns an hex-encoded String of the specified byte array.
     *
     * @param array The byte array to be hex-encoded.
     * @param offset The start position.
     * @param length The number of bytes to be converted.
     * @return A hex-encoded String of the specified byte array.
     */
    public static String byteArrayToHexString(byte[] bytes, int offset, int length) {
        if (bytes == null || length == 0) {
            return EMPTY_STRING;
        }
        StringBuilder sb = new StringBuilder();

        for (int i = 0; i < length; i++) {
            sb.append(String.format("%02x", bytes[offset + i] & 0xFF));
        }
        return sb.toString();
    }

    /**
     * Converts a String to a byte array
     *
     * @param str The string to be converted to byte array
     * @return A byte array of the specified string.
     */
    public static byte[] hexStringToByteArray(final String str) {
        if ((str == null || str.length() == 0)) {
            return null;
        }
        final int length = str.length();
        if (length % 2 != 0) {
            throw new NumberFormatException(
                    "Input hex string length must be an even number." + " Length = " + length);
        }
        final byte[] bytes = new byte[length / 2];
        for (int i = 0; i < length; i += 2) {
            bytes[i / 2] =
                    (byte)
                            ((Character.digit(str.charAt(i), 16) << 4)
                                    + Character.digit(str.charAt(i + 1), 16));
        }
        return bytes;
    }
}
