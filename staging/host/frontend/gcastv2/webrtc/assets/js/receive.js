'use strict';

const receiveButton = document.getElementById('receiveButton');
receiveButton.addEventListener('click', onReceive);
const keyboardCaptureButton = document.getElementById('keyboardCaptureBtn');
keyboardCaptureButton.addEventListener('click', onKeyboardCaptureClick);

const deviceScreen = document.getElementById('deviceScreen');

deviceScreen.addEventListener("click", onInitialClick);

function onInitialClick(e) {
    // This stupid thing makes sure that we disable controls after the first click...
    // Why not just disable controls altogether you ask? Because then audio won't play
    // because these days user-interaction is required to enable audio playback...
    console.log("onInitialClick");

    deviceScreen.controls = false;
    deviceScreen.removeEventListener("click", onInitialClick);
}

let pc1;
let pc2;

let dataChannel;

let ws;

let offerResolve;
let iceCandidateResolve;

let videoStream;

let mouseIsDown = false;

const is_chrome = navigator.userAgent.indexOf("Chrome") !== -1;

function handleDataChannelStatusChange(event) {
    console.log('handleDataChannelStatusChange state=' + dataChannel.readyState);

    if (dataChannel.readyState == "open") {
        dataChannel.send("Hello, world!");
    }
}

function handleDataChannelMessage(event) {
    console.log('handleDataChannelMessage data="' + event.data + '"');
}

function onKeyboardCaptureClick(e) {
    const selectedClass = 'selected';
    if (keyboardCaptureButton.classList.contains(selectedClass)) {
        stopKeyboardTracking();
        keyboardCaptureButton.classList.remove(selectedClass);
    } else {
        startKeyboardTracking();
        keyboardCaptureButton.classList.add(selectedClass);
    }
}

async function onReceive() {
    console.log('onReceive');
    receiveButton.disabled = true;

    init_logcat();

    const wsProtocol = (location.protocol == "http:") ? "ws:" : "wss:";

    ws = new WebSocket(wsProtocol + "//" + location.host + "/control");
    // temporarily disable audio to free ports in the server since it's only
    // producing silence anyways.
    var search = location.search + "&disable_audio=1";
    search = '?' + search.substr(1);

    ws.onopen = function() {
        console.log("onopen");
        ws.send('{\r\n'
            +     '"type": "greeting",\r\n'
            +     '"message": "Hello, world!",\r\n'
            +     '"path": "' + location.pathname + search + '"\r\n'
            +   '}');
    };
    ws.onmessage = function(e) {
        console.log("onmessage " + e.data);

        let data = JSON.parse(e.data);
        if (data.type == "hello") {
            kickoff();
        } else if (data.type == "offer" && offerResolve) {
            offerResolve(data.sdp);
            offerResolve = undefined;
        } else if (data.type == "ice-candidate" && iceCandidateResolve) {
            iceCandidateResolve(data);

            iceCandidateResolve = undefined;
        }
    };

    pc2 = new RTCPeerConnection();
    console.log('got pc2=' + pc2);

    pc2.addEventListener(
        'icecandidate', e => onIceCandidate(pc2, e));

    pc2.addEventListener(
        'iceconnectionstatechange', e => onIceStateChange(pc2, e));

    pc2.addEventListener(
        'connectionstatechange', e => {
        console.log("connection state = " + pc2.connectionState);
    });

    pc2.addEventListener('track', onGotRemoteStream);

    dataChannel = pc2.createDataChannel("data-channel");
    dataChannel.onopen = handleDataChannelStatusChange;
    dataChannel.onclose = handleDataChannelStatusChange;
    dataChannel.onmessage = handleDataChannelMessage;
}

async function kickoff() {
    console.log('createOffer start');

    try {
        var offer = await getWsOffer();
        await onCreateOfferSuccess(offer);
    } catch (e) {
        console.log('createOffer FAILED ');
    }
}

async function onCreateOfferSuccess(desc) {
    console.log(`Offer ${desc.sdp}`);

    try {
        pc2.setRemoteDescription(desc);
    } catch (e) {
        console.log('setRemoteDescription pc2 FAILED');
        return;
    }

    console.log('setRemoteDescription pc2 successful.');

    try {
        setWsLocalDescription(desc);
    } catch (e) {
        console.log('setLocalDescription pc1 FAILED');
        return;
    }

    console.log('setLocalDescription pc1 successful.');

    try {
        const answer = await pc2.createAnswer();

        await onCreateAnswerSuccess(answer);
    } catch (e) {
        console.log('createAnswer FAILED');
    }
}

function setWsRemoteDescription(desc) {
    ws.send('{\r\n'
        +     '"type": "set-remote-desc",\r\n'
        +     '"sdp": "' + desc.sdp + '"\r\n'
        +   '}');
}

function setWsLocalDescription(desc) {
    ws.send('{\r\n'
        +     '"type": "set-local-desc",\r\n'
        +     '"sdp": "' + desc.sdp + '"\r\n'
        +   '}');
}

async function getWsOffer() {
    const offerPromise = new Promise(function(resolve, reject) {
        offerResolve = resolve;
    });

    ws.send('{\r\n'
        +     '"type": "request-offer",\r\n'
        +     (is_chrome ? '"is_chrome": 1\r\n'
                         : '"is_chrome": 0\r\n')
        +   '}');

    const sdp = await offerPromise;

    return { type: "offer", sdp: sdp };
}

async function getWsIceCandidate(mid) {
    console.log("getWsIceCandidate (mid=" + mid + ")");

    const answerPromise = new Promise(function(resolve, reject) {
        iceCandidateResolve = resolve;
    });

    ws.send('{\r\n'
        +     '"type": "get-ice-candidate",\r\n'
        +     '"mid": ' + mid + ',\r\n'
        +   '}');

    const replyInfo = await answerPromise;

    console.log("got replyInfo '" + replyInfo + "'");

    if (replyInfo == undefined || replyInfo.candidate == undefined) {
        return null;
    }

    const replyCandidate = replyInfo.candidate;
    const mlineIndex = replyInfo.mlineIndex;

    let result;
    try {
        result = new RTCIceCandidate(
            {
                sdpMid: mid,
                sdpMLineIndex: mlineIndex,
                candidate: replyCandidate
            });
    }
    catch (e) {
        console.log("new RTCIceCandidate FAILED. " + e);
        return undefined;
    }

    console.log("got result " + result);

    return result;
}

async function addRemoteIceCandidate(mid) {
    const candidate = await getWsIceCandidate(mid);

    if (!candidate) {
        return false;
    }

    try {
        await pc2.addIceCandidate(candidate);
    } catch (e) {
        console.log("addIceCandidate pc2 FAILED w/ " + e);
        return false;
    }

    console.log("addIceCandidate pc2 successful. (mid="
        + mid + ", mlineIndex=" + candidate.sdpMLineIndex + ")");

    return true;
}

async function onCreateAnswerSuccess(desc) {
    console.log(`Answer ${desc.sdp}`);

    try {
        await pc2.setLocalDescription(desc);
    } catch (e) {
        console.log('setLocalDescription pc2 FAILED ' + e);
        return;
    }

    console.log('setLocalDescription pc2 successful.');

    try {
        setWsRemoteDescription(desc);
    } catch (e) {
        console.log('setRemoteDescription pc1 FAILED');
        return;
    }

    console.log('setRemoteDescription pc1 successful.');

    if (!await addRemoteIceCandidate(0)) {
        return;
    }
    await addRemoteIceCandidate(1);
    await addRemoteIceCandidate(2);
}

function getPcName(pc) {
    return ((pc == pc2) ? "pc2" : "pc1");
}

async function onIceCandidate(pc, e) {
    console.log(
        getPcName(pc)
        + ' onIceCandidate '
        + (e.candidate ? ('"' + e.candidate.candidate + '"') : '(null)')
        + " "
        + (e.candidate ? ('sdmMid: ' + e.candidate.sdpMid) : '(null)')
        + " "
        + (e.candidate ? ('sdpMLineIndex: ' + e.candidate.sdpMLineIndex) : '(null)'));

    if (!e.candidate) {
        return;
    }

    let other_pc = (pc == pc2) ? pc1 : pc2;

    if (other_pc) {
        try {
            await other_pc.addIceCandidate(e.candidate);
        } catch (e) {
            console.log('addIceCandidate FAILED ' + e);
            return;
        }

        console.log('addIceCandidate successful.');
    }
}

async function onIceStateChange(pc, e) {
    console.log(
        'onIceStateChange ' + getPcName(pc) + " '" + pc.iceConnectionState + "'");

    if (pc.iceConnectionState == "connected") {
        deviceScreen.srcObject = videoStream;

        startMouseTracking()
    } else if (pc.iceConnectionState == "disconnected") {
        stopMouseTracking()
    }
}

async function onGotRemoteStream(e) {
    console.log('onGotRemoteStream ' + e);

    const track = e.track;

    console.log('track = ' + track);
    console.log('track.kind = ' + track.kind);
    console.log('track.readyState = ' + track.readyState);
    console.log('track.enabled = ' + track.enabled);

    if (track.kind == "video") {
        videoStream = e.streams[0];
    }
}

function startMouseTracking() {
    if (window.PointerEvent) {
        deviceScreen.addEventListener("pointerdown", onStartDrag);
        deviceScreen.addEventListener("pointermove", onContinueDrag);
        deviceScreen.addEventListener("pointerup", onEndDrag);
    } else if (window.TouchEvent) {
        deviceScreen.addEventListener("touchstart", onStartDrag);
        deviceScreen.addEventListener("touchmove", onContinueDrag);
        deviceScreen.addEventListener("touchend", onEndDrag);
    } else if (window.MouseEvent) {
        deviceScreen.addEventListener("mousedown", onStartDrag);
        deviceScreen.addEventListener("mousemove", onContinueDrag);
        deviceScreen.addEventListener("mouseup", onEndDrag);
    }
}

function stopMouseTracking() {
    if (window.PointerEvent) {
        deviceScreen.removeEventListener("pointerdown", onStartDrag);
        deviceScreen.removeEventListener("pointermove", onContinueDrag);
        deviceScreen.removeEventListener("pointerup", onEndDrag);
    } else if (window.TouchEvent) {
        deviceScreen.removeEventListener("touchstart", onStartDrag);
        deviceScreen.removeEventListener("touchmove", onContinueDrag);
        deviceScreen.removeEventListener("touchend", onEndDrag);
    } else if (window.MouseEvent) {
        deviceScreen.removeEventListener("mousedown", onStartDrag);
        deviceScreen.removeEventListener("mousemove", onContinueDrag);
        deviceScreen.removeEventListener("mouseup", onEndDrag);
    }
}

function startKeyboardTracking() {
    document.addEventListener('keydown', onKeyEvent);
    document.addEventListener('keyup', onKeyEvent);
}

function stopKeyboardTracking() {
    document.removeEventListener('keydown', onKeyEvent);
    document.removeEventListener('keyup', onKeyEvent);
}

function onStartDrag(e) {
    e.preventDefault();

    // console.log("mousedown at " + e.pageX + " / " + e.pageY);
    mouseIsDown = true;

    sendMouseUpdate(true, e);
}

function onEndDrag(e) {
    e.preventDefault();

    // console.log("mouseup at " + e.pageX + " / " + e.pageY);
    mouseIsDown = false;

    sendMouseUpdate(false, e);
}

function onContinueDrag(e) {
    e.preventDefault();

    // console.log("mousemove at " + e.pageX + " / " + e.pageY + ", down=" + mouseIsDown);
    if (mouseIsDown) {
        sendMouseUpdate(true, e);
    }
}

function sendMouseUpdate(down, e) {
    var x = e.offsetX;
    var y = e.offsetY;

    const videoWidth = deviceScreen.videoWidth;
    const videoHeight = deviceScreen.videoHeight;
    const elementWidth = deviceScreen.width;
    const elementHeight = deviceScreen.height;

    // vh*ew > eh*vw? then scale h instead of w
    const scaleHeight = videoHeight * elementWidth > videoWidth * elementHeight;
    var elementScaling = 0, videoScaling = 0;
    if (scaleHeight) {
        elementScaling = elementHeight;
        videoScaling = videoHeight;
    } else {
        elementScaling = elementWidth;
        videoScaling = videoWidth;
    }

    // Substract the offset produced by the difference in aspect ratio if any.
    if (scaleHeight) {
        x -= (elementWidth - elementScaling * videoWidth / videoScaling) / 2;
    } else {
        y -= (elementHeight - elementScaling * videoHeight / videoScaling) / 2;
    }

    // Convert to coordinates relative to the video
    x = videoScaling * x / elementScaling;
    y = videoScaling * y / elementScaling;

    ws.send('{\r\n'
        +     '"type": "set-mouse-position",\r\n'
        +     '"down": ' + (down ? "1" : "0") + ',\r\n'
        +     '"x": ' + Math.trunc(x) + ',\r\n'
        +     '"y": ' + Math.trunc(y) + '\r\n'
        +   '}');
}

function onKeyEvent(e) {
    e.preventDefault();
    ws.send('{"type": "key-event","keycode": "'+e.code+'", "event_type": "'+e.type+'"}');
}