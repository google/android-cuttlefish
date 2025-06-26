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

import com.android.javacard.keymaster.KMJCardSimApplet;

import com.licel.jcardsim.smartcardio.CardSimulator;
import com.licel.jcardsim.utils.AIDUtil;

import javacard.framework.AID;
import javacard.framework.ISO7816;

import java.io.IOException;
import java.util.Vector;

import javax.smartcardio.CommandAPDU;
import javax.smartcardio.ResponseAPDU;

/**
 * This class manages JCardSim operations: channel setup, applet installation, simulator
 * initialization/reset, and APDU transmission.
 */
public class JCardSimulator implements Simulator {
    public static final int MAX_LOGICAL_CHANNEL = 4;
    public static final byte INS_SELECT = (byte) 0xA4;
    public static final byte INS_MANAGE_CHANNEL = (byte) 0x70;
    // KeyMint Applet AID
    public static final String KEYMINT_AID = "A00000006203020C010101";

    private CardSimulator simulator;
    private ResponseAPDU response;
    private Vector<String> channelAid;
    private int currentChannel;

    public JCardSimulator() {
        // Creating an empty Vector
        channelAid = new Vector<String>(MAX_LOGICAL_CHANNEL);
        channelAid.add("ZeroChannelOccupied");
        for (int ch = 1; ch < MAX_LOGICAL_CHANNEL; ch++) {
            channelAid.add(null);
        }
        currentChannel = -1;
    }

    @Override
    public void initializeSimulator() throws Exception {
        // Create simulator
        simulator = new CardSimulator();
    }

    @Override
    public void disconnectSimulator() throws Exception {
        currentChannel = -1;
    }

    private void installKeyMint() throws Exception {
        AID appletAID = AIDUtil.create(KEYMINT_AID);
        simulator.installApplet(appletAID, KMJCardSimApplet.class);
        // Select applet
        simulator.selectApplet(appletAID);
        // Provision
        new KeymintSEFactoryProvision(simulator).provision();
        new KeymintOEMProvision(simulator).provision();
    }

    @Override
    public void setupSimulator() throws Exception {
        installKeyMint();
    }

    private final byte[] intToByteArray(int value) {
        return new byte[] {(byte) (value >>> 8), (byte) value};
    }

    private byte getchannelNumber(byte cla) throws IOException {
        byte ch = (byte) (cla & 0x03);
        boolean b7 = (cla & 0x40) == (byte) 0x40;

        // b7 = 1 indicates the inter-industry class byte coding
        if (b7) {
            ch -= 4;
        }

        if (!(ch >= (byte) 0x00 && ch <= (byte) 0x14)) {
            throw new IOException("class byte error");
        }

        return ch;
    }

    private ResponseAPDU processManageCommand(byte[] apdu) {
        int firstAvailableSlot = -1;
        int numChannels = channelAid.size();

        // Close the channel if p1 = 0x80
        if (apdu[ISO7816.OFFSET_P1] == (byte) 0x80) {
            channelAid.set(apdu[ISO7816.OFFSET_P2], null);
            return new ResponseAPDU(new byte[] {(byte) 0x90, 0x00});
        }

        for (int i = 1; i < numChannels; i++) {
            if (channelAid.get(i) == null) {
                firstAvailableSlot = i;
                break;
            }
        }

        if (firstAvailableSlot == -1) {
            return new ResponseAPDU(new byte[] {(byte) 0x68, (byte) 0x81});
        }

        currentChannel = firstAvailableSlot;
        return new ResponseAPDU(new byte[] {(byte) currentChannel, (byte) 0x90, 0x00});
    }

    /*
     * Jcard Simulator design is based on one applet and one channel at a time
     *
     * In order to communicate multiple applets simultaneously on different channels
     * We have added Logical channels implementation here. which has following variables
     *  - Vector[AID] (index 0 represent channel 0... so on)
     *  - CurrentChannelnumber
     * Generalized flow between SE hal and SE applet via JCserver is as follow
     *
     *    SE HAL                     JCServer                                     JcardSim
     *  ------------------------------------------------------------------------------------------
     *  Managechannel ->         check if any channel is
     *                           free, if yes set occupied
     *                           and return channel number.
     *                           Else Error
     *
     *  Select Cmd    ->         select Command                              -->    select cmd
     *
     *                           if success copy AID to
     *                           respective array and set
     *                           CurrentChannelnumber = CH(CLA)
     *
     *
     *  Non-Select Cmd ->      if (CH(CLA) == CurrentChannelnumber)
     *                             send "Non-Select" cmd                    --> "Non-Select" cmd
     *                         else
     *                             send "select(AID(CH(CLA))"               -->  select cmd
     *                                  "CurrentChannelnumber = CH(CLA)"
     *                             send "Non-Select" cmd                    --> "Non-Select" cmd
     */
    @Override
    public byte[] executeApdu(byte[] apdu) throws Exception {

        // Check if ManageChannel Command
        if (apdu[ISO7816.OFFSET_INS] == INS_MANAGE_CHANNEL) {
            response = processManageCommand(apdu);
        } else {
            CommandAPDU apduCmd = new CommandAPDU(apdu);
            byte ch = getchannelNumber((byte) apduCmd.getCLA());

            if (ch == currentChannel || (byte) apduCmd.getINS() == INS_SELECT) {
                response = simulator.transmitCommand(apduCmd);
                // save AIDs if command is select
                if ((byte) apduCmd.getINS() == INS_SELECT && response.getSW() == (int) 0x9000) {
                    channelAid.set(
                            ch,
                            ByteArrayConverter.byteArrayToHexString(
                                    apdu, ISO7816.OFFSET_CDATA, apdu[ISO7816.OFFSET_LC]));
                    currentChannel = ch;
                }
            } else {
                // send select command
                byte[] aid = ByteArrayConverter.hexStringToByteArray(channelAid.get(ch));
                byte[] selApdu = new byte[6 + aid.length];
                selApdu[0] = 0x00;
                selApdu[1] = INS_SELECT;
                selApdu[2] = (byte) 0x04;
                selApdu[3] = (byte) 0x00;
                selApdu[4] = (byte) aid.length;
                System.arraycopy(aid, 0, selApdu, 5, aid.length);
                selApdu[selApdu.length - 1] = 0x00;

                CommandAPDU selectCmd = new CommandAPDU(selApdu);
                response = simulator.transmitCommand(selectCmd);
                if (response.getSW() == 0x9000) {
                    currentChannel = ch;
                    response = simulator.transmitCommand(apduCmd);
                }
            }
        }
        return intToByteArray(response.getSW());
    }

    @Override
    public byte[] formatApduResponse() {
        byte[] resp = response.getData();
        byte[] status = intToByteArray(response.getSW());
        byte[] out = new byte[(resp.length + status.length)];
        System.arraycopy(resp, 0, out, 0, resp.length);
        System.arraycopy(status, 0, out, resp.length, status.length);
        return out;
    }
}
