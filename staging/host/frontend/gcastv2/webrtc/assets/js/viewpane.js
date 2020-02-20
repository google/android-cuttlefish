// Resize button will control the execution of the resizeDeviceScreen function
const resizeButton = document.getElementById('resizeButton');
resizeButton.addEventListener('click', resizeDeviceScreen);

// This will be used to set the max size for the video element.
// Ex. 0.9 means that the video element will be at most 90% of the windowSize
const maxVideoRatio = 0.9;

function resizeDeviceScreen() {
    // Capture/derive all the relevant dimensions
    var viewportHeight = window.innerHeight;
    var viewportWidth = window.innerWidth;
    var maxHeight = viewportHeight * maxVideoRatio;
    var maxWidth = viewportWidth * maxVideoRatio;
    var videoWidth = deviceScreen.videoWidth;
    var videoHeight = deviceScreen.videoHeight;

    if (videoHeight <= maxHeight && videoWidth <= maxWidth) {
        deviceScreen.setAttribute('height', videoHeight);
        deviceScreen.setAttribute('width', videoWidth);
    }
    else {
        deviceScreen.setAttribute('height', maxHeight);
        deviceScreen.setAttribute('width', maxWidth);
    }
}

// Listen for the 'play' event on the primary video element
deviceScreen.addEventListener('play', resizeDeviceScreen);