let adb_ws;
let logcat = document.getElementById('logcat');

let utf8Encoder = new TextEncoder();
let utf8Decoder = new TextDecoder();

const A_CNXN = 0x4e584e43;
const A_OPEN = 0x4e45504f;
const A_WRTE = 0x45545257;
const A_OKAY = 0x59414b4f;

const kLocalChannelId = 666;

function setU32LE(array, offset, x) {
    array[offset] = x & 0xff;
    array[offset + 1] = (x >> 8) & 0xff;
    array[offset + 2] = (x >> 16) & 0xff;
    array[offset + 3] = x >> 24;
}

function getU32LE(array, offset) {
    let x = array[offset]
        | (array[offset + 1] << 8)
        | (array[offset + 2] << 16)
        | (array[offset + 3] << 24);

    return x >>> 0;  // convert signed to unsigned if necessary.
}

function computeChecksum(array) {
    let sum = 0;
    let i;
    for (i = 0; i < array.length; ++i) {
        sum = ((sum + array[i]) & 0xffffffff) >>> 0;
    }

    return sum;
}

function createAdbMessage(command, arg0, arg1, payload) {
    let arrayBuffer = new ArrayBuffer(24 + payload.length);
    let array = new Uint8Array(arrayBuffer);
    setU32LE(array, 0, command);
    setU32LE(array, 4, arg0);
    setU32LE(array, 8, arg1);
    setU32LE(array, 12, payload.length);
    setU32LE(array, 16, computeChecksum(payload));
    setU32LE(array, 20, command ^ 0xffffffff);
    array.set(payload, 24);

    return arrayBuffer;
}

function adbOpenConnection() {
    let systemIdentity = utf8Encoder.encode("Cray_II:1234:whatever");

    let arrayBuffer = createAdbMessage(
        A_CNXN, 0x1000000, 256 * 1024, systemIdentity);

    adb_ws.send(arrayBuffer);
}

function adbOpenChannel() {
    let destination = utf8Encoder.encode("shell:logcat");

    let arrayBuffer = createAdbMessage(A_OPEN, kLocalChannelId, 0, destination);
    adb_ws.send(arrayBuffer);
}

function adbSendOkay(remoteId) {
    let payload = new Uint8Array(0);

    let arrayBuffer = createAdbMessage(
        A_OKAY, kLocalChannelId, remoteId, payload);

    adb_ws.send(arrayBuffer);
}

function adbOnMessage(ev) {
    // console.log("adb_ws: onmessage (" + ev.data.byteLength + " bytes)");

    let arrayBuffer = ev.data;
    let array = new Uint8Array(arrayBuffer);

    if (array.length < 24) {
        console.log("adb message too short.");
        return;
    }

    let command = getU32LE(array, 0);
    let magic = getU32LE(array, 20);

    if (command != ((magic ^ 0xffffffff) >>> 0)) {
        console.log("command = " + command + ", magic = " + magic);
        console.log("adb message command vs magic failed.");
        return;
    }

    let payloadLength = getU32LE(array, 12);

    if (array.length != 24 + payloadLength) {
        console.log("adb message length mismatch.");
        return;
    }

    let payloadChecksum = getU32LE(array, 16);
    let checksum = computeChecksum(array.slice(24));

    if (payloadChecksum != checksum) {
        console.log("adb message checksum mismatch.");
        return;
    }

    switch (command) {
        case A_CNXN:
        {
            console.log("connected.");

            adbOpenChannel();
            break;
        }

        case A_OKAY:
        {
            let remoteId = getU32LE(array, 4);
            console.log("channel created w/ remoteId " + remoteId);
            break;
        }

        case A_WRTE:
        {
            let payloadText = utf8Decoder.decode(array.slice(24));

            logcat.value += payloadText;

            // Scroll to bottom
            logcat.scrollTop = logcat.scrollHeight;

            let remoteId = getU32LE(array, 4);
            adbSendOkay(remoteId);
            break;
        }
    }
}

function init_logcat() {
    const wsProtocol = (location.protocol == "http:") ? "ws:" : "wss:";

    adb_ws = new WebSocket(
        wsProtocol + "//" + location.host + "/control_adb");

    adb_ws.binaryType = "arraybuffer";

    adb_ws.onopen = function() {
        console.log("adb_ws: onopen");

        adbOpenConnection();

        logcat.style.display = "initial";
    };
    adb_ws.onmessage = adbOnMessage;
}
