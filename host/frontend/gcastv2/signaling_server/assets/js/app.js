'use strict';

function ConnectToDevice(device_id, use_tcp) {
  console.log('ConnectToDevice ', device_id);
  const keyboardCaptureButton = document.getElementById('keyboardCaptureBtn');
  keyboardCaptureButton.addEventListener('click', onKeyboardCaptureClick);

  const deviceScreen = document.getElementById('deviceScreen');
  deviceScreen.addEventListener('click', onInitialClick);

  function onInitialClick(e) {
    // This stupid thing makes sure that we disable controls after the first
    // click... Why not just disable controls altogether you ask? Because then
    // audio won't play because these days user-interaction is required to enable
    // audio playback...
    console.log('onInitialClick');

    deviceScreen.controls = false;
    deviceScreen.removeEventListener('click', onInitialClick);
  }

  let videoStream;
  let mouseIsDown = false;
  let deviceConnection;

  let logcatBtn = document.getElementById('showLogcatBtn');
  logcatBtn.onclick = ev => {
    init_logcat(deviceConnection);
    logcatBtn.remove();
  };

  let options = {
    // temporarily disable audio to free ports in the server since it's only
    // producing silence anyways.
    disable_audio: true,
    wsUrl: ((location.protocol == 'http:') ? 'ws://' : 'wss://') +
      location.host + '/connect_client',
    use_tcp,
  };
  let urlParams = new URLSearchParams(location.search);
  for (const [key, value] of urlParams) {
    options[key] = JSON.parse(value);
  }

  import('./cf_webrtc.js')
    .then(webrtcModule => webrtcModule.Connect(device_id, options))
    .then(devConn => {
      deviceConnection = devConn;
      // TODO(b/143667633): get multiple display configuration from the
      // description object
      console.log(deviceConnection.description);
      let stream_id = devConn.description.displays[0].stream_id;
      devConn.getStream(stream_id).then(stream => {
        videoStream = stream;
        deviceScreen.srcObject = videoStream;
      }).catch(e => console.error('Unable to get display stream: ', e));
      startMouseTracking();  // TODO stopMouseTracking() when disconnected
  });

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

  function startMouseTracking() {
    if (window.PointerEvent) {
      deviceScreen.addEventListener('pointerdown', onStartDrag);
      deviceScreen.addEventListener('pointermove', onContinueDrag);
      deviceScreen.addEventListener('pointerup', onEndDrag);
    } else if (window.TouchEvent) {
      deviceScreen.addEventListener('touchstart', onStartDrag);
      deviceScreen.addEventListener('touchmove', onContinueDrag);
      deviceScreen.addEventListener('touchend', onEndDrag);
    } else if (window.MouseEvent) {
      deviceScreen.addEventListener('mousedown', onStartDrag);
      deviceScreen.addEventListener('mousemove', onContinueDrag);
      deviceScreen.addEventListener('mouseup', onEndDrag);
    }
  }

  function stopMouseTracking() {
    if (window.PointerEvent) {
      deviceScreen.removeEventListener('pointerdown', onStartDrag);
      deviceScreen.removeEventListener('pointermove', onContinueDrag);
      deviceScreen.removeEventListener('pointerup', onEndDrag);
    } else if (window.TouchEvent) {
      deviceScreen.removeEventListener('touchstart', onStartDrag);
      deviceScreen.removeEventListener('touchmove', onContinueDrag);
      deviceScreen.removeEventListener('touchend', onEndDrag);
    } else if (window.MouseEvent) {
      deviceScreen.removeEventListener('mousedown', onStartDrag);
      deviceScreen.removeEventListener('mousemove', onContinueDrag);
      deviceScreen.removeEventListener('mouseup', onEndDrag);
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

    // console.log("mousemove at " + e.pageX + " / " + e.pageY + ", down=" +
    // mouseIsDown);
    if (mouseIsDown) {
      sendMouseUpdate(true, e);
    }
  }

  function sendMouseUpdate(down, e) {
    console.assert(deviceConnection, 'Can\'t send mouse update without device');
    var x = e.offsetX;
    var y = e.offsetY;

    // Before the first video frame arrives there is no way to know width and
    // height of the device's screen, so turn every click into a click at 0x0.
    // A click at that position is not more dangerous than anywhere else since
    // the user is clicking blind anyways.
    const videoWidth = deviceScreen.videoWidth? deviceScreen.videoWidth: 1;
    const videoHeight = deviceScreen.videoHeight? deviceScreen.videoHeight: 1;
    const elementWidth = deviceScreen.offsetWidth? deviceScreen.offsetWidth: 1;
    const elementHeight = deviceScreen.offsetHeight? deviceScreen.offsetHeight: 1;

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

    deviceConnection.sendMousePosition(
        {x: Math.trunc(x), y: Math.trunc(y), down});
  }

  function onKeyEvent(e) {
    e.preventDefault();
    console.assert(deviceConnection, 'Can\'t send key event without device');
    deviceConnection.sendKeyEvent(e.code, e.type);
  }
}

/******************************************************************************/

function ConnectDeviceCb(dev_id, use_tcp) {
  console.log('Connect: ' + dev_id);
  // Hide the device selection screen
  document.getElementById('device_selector').style.display = 'none';
  // Show the device control screen
  document.getElementById('device_connection').style.visibility = 'visible';
  ConnectToDevice(dev_id, use_tcp);
}

function ShowNewDeviceList(device_ids) {
  let ul = document.getElementById('device_list');
  ul.innerHTML = "";
  for (const dev_id of device_ids) {
    ul.innerHTML += ('<li class="device_entry" title="Connect to ' + dev_id
                     + '">' + dev_id + '<button onclick="ConnectDeviceCb(\''
                     + dev_id + '\', false)">Connect</button><button '
                     + 'onclick="ConnectDeviceCb(\'' + dev_id + '\', true)"'
                     + ' title="Useful when a proxy or firewall forbid UDP '
                     + 'connections">Connect over TCP only</button></li>');
  }
}

function UpdateDeviceList() {
  let url = ((location.protocol == 'http:') ? 'ws:' : 'wss:') + location.host +
    '/list_devices';
  let ws = new WebSocket(url);
  ws.onopen = () => {
    ws.send("give me those device ids");
  };
  ws.onmessage = msg => {
   let device_ids = JSON.parse(msg.data);
    ShowNewDeviceList(device_ids);
  };
}

// Get any devices that are already connected
UpdateDeviceList();
// Update the list at the user's request
document.getElementById('refresh_list').onclick = evt => UpdateDeviceList();
