#import "CameraSourceSupport.h"

@implementation MyVideoOutputDelegate

-(id)initWithCallback:(CameraSessionCallback)cb cookie:(void *)cookie {
    if ((self = [super init]) != nil) {
        first_ = YES;

        cb_ = cb;
        cookie_ = cookie;

        paused_ = false;

#ifdef TARGET_IOS
        compressionSession_ = nil;
#endif
    }

    return self;
}

-(void)pause {
    paused_ = true;
}

-(void)resume {
    paused_ = false;
}

-(void)onCompressedFrame:(CMSampleBufferRef)buffer {
    if (first_) {
        first_ = NO;

        CMFormatDescriptionRef format =
            CMSampleBufferGetFormatDescription(buffer);

        size_t numParameterSets;
        OSStatus err = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
                format, 0 /* index */, NULL, NULL, &numParameterSets, NULL);
        NSAssert(
                err == noErr,
                @"CMVideoFormatDescriptionGetH264ParameterSetAtIndex failed");

        const uint8_t *params;
        size_t paramsSize;
        for (size_t i = 0; i < numParameterSets; ++i) {
            err = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
                    format, i, &params, &paramsSize, NULL, NULL);
            NSAssert(
                    err == noErr,
                    @"CMVideoFormatDescriptionGetH264ParameterSetAtIndex failed");

            cb_(cookie_, i, 0ll, params, paramsSize);
        }
    }

    CMItemCount numSamples = CMSampleBufferGetNumSamples(buffer);
    NSAssert(numSamples == 1, @"expected a single sample");

    CMBlockBufferRef blockBuffer = CMSampleBufferGetDataBuffer(buffer);

    size_t sampleSize = CMSampleBufferGetSampleSize(buffer, 0 /* index */);

    size_t lengthAtOffset;
    size_t totalLength;
    char *ptr;
    OSStatus err = CMBlockBufferGetDataPointer(
            blockBuffer, 0 /* offset */, &lengthAtOffset, &totalLength, &ptr);
    NSAssert(err == kCMBlockBufferNoErr, @"CMBlockBufferGetDataPointer failed");
    NSAssert(lengthAtOffset == sampleSize, @"sampleSize mismatch");
    NSAssert(lengthAtOffset <= totalLength, @"totalLength mismatch");

    // NSLog(@"  sample has size %zu, ptr:%p", sampleSize, ptr);

    CMSampleTimingInfo info;
    err = CMSampleBufferGetSampleTimingInfo(buffer, 0 /* sampleIndex */, &info);
    NSAssert(err == noErr, @"CMSampleBufferGetSampleTimingInfo failed");

    const int64_t timeUs =
        (int64_t)(CMTimeGetSeconds(info.presentationTimeStamp) * 1e6);

    cb_(cookie_, -1, timeUs, ptr, sampleSize);
}

#ifdef TARGET_IOS
-(void)dealloc {
    VTCompressionSessionInvalidate(compressionSession_);
    compressionSession_ = nil;
}

void CompressionCallback(
        void *cookie,
        void *sourceFrameRefCon,
        OSStatus status,
        VTEncodeInfoFlags infoFlags,
        CMSampleBufferRef buffer) {
    [(__bridge MyVideoOutputDelegate *)cookie onCompressedFrame:buffer];
}

-(void)captureOutput:(AVCaptureOutput *)output
       didOutputSampleBuffer:(CMSampleBufferRef)buffer
       fromConnection:(AVCaptureConnection *)connection {
    if (paused_) {
        return;
    }
    
    CMFormatDescriptionRef format =
        CMSampleBufferGetFormatDescription(buffer);

    if (compressionSession_ == nil) {
        CMVideoDimensions dim = CMVideoFormatDescriptionGetDimensions(format);

        OSStatus err = VTCompressionSessionCreate(
                kCFAllocatorDefault,
                dim.width,
                dim.height,
                kCMVideoCodecType_H264,
                NULL /* encoderSettings */,
                NULL /* sourceImageBufferAttributes */,
                kCFAllocatorDefault,
                CompressionCallback,
                (__bridge void *)self,
                &compressionSession_);

        NSAssert(err == noErr, @"VTCompressionSessionCreate failed");
    }

    CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(buffer);

    CMSampleTimingInfo info;
    OSStatus err = CMSampleBufferGetSampleTimingInfo(buffer, 0 /* sampleIndex */, &info);
    NSAssert(err == noErr, @"CMSampleBufferGetSampleTimingInfo failed");

    VTEncodeInfoFlags infoFlags;
    err = VTCompressionSessionEncodeFrame(
            compressionSession_,
            imageBuffer,
            info.presentationTimeStamp,
            info.duration,
            NULL /* frameProperties */,
            NULL /* sourceFrameRefCon */,
            &infoFlags);

    NSAssert(err == noErr, @"VTCompressionSessionEncodeFrame failed");
}
#else
-(void)captureOutput:(AVCaptureOutput *)output
       didOutputSampleBuffer:(CMSampleBufferRef)buffer
       fromConnection:(AVCaptureConnection *)connection {
    [self onCompressedFrame:buffer];
}
#endif

-(void)captureOutput:(AVCaptureOutput *)output
 didDropSampleBuffer:(CMSampleBufferRef)buffer
 fromConnection:(AVCaptureConnection *)connection {
     NSLog(@"Dropped a frame!");
}

@end

@implementation MyCameraSession

-(id)initWithCallback:(CameraSessionCallback)cb cookie:(void *)cookie {
    if ((self = [super init]) != nil) {
        dispatchQueue_ = dispatch_queue_create(
                "CameraSource", DISPATCH_QUEUE_SERIAL);

        session_ = [AVCaptureSession new];
        session_.sessionPreset = AVCaptureSessionPreset1280x720;

        [session_ beginConfiguration];

#ifdef TARGET_IOS
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                                 completionHandler:^(BOOL granted) {
                                     NSLog(@"XXX granted = %d", granted);
                                 }];

        camera_ = [AVCaptureDevice defaultDeviceWithDeviceType:AVCaptureDeviceTypeBuiltInDualCamera mediaType:AVMediaTypeVideo position:AVCaptureDevicePositionFront];

        if (camera_ == nil) {
            camera_ = [AVCaptureDevice defaultDeviceWithDeviceType:AVCaptureDeviceTypeBuiltInWideAngleCamera mediaType:AVMediaTypeVideo position:AVCaptureDevicePositionFront];
        }
#else
        camera_ = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
#endif

        NSLog(@"camera supports the following frame rates: %@",
              camera_.activeFormat.videoSupportedFrameRateRanges);
        
        camera_.activeVideoMinFrameDuration = CMTimeMake(1, 30);  // limit to 30fps
        
        NSError *error;
        AVCaptureDeviceInput *videoInput =
            [AVCaptureDeviceInput deviceInputWithDevice:camera_ error:&error];

        AVCaptureVideoDataOutput *videoOutput = [AVCaptureVideoDataOutput new];

        // NSLog(@"available codec types: %@", videoOutput.availableVideoCodecTypes);
        // NSLog(@"available cvpixel types: %@", videoOutput.availableVideoCVPixelFormatTypes);

#if defined(TARGET_IOS)
        videoOutput.videoSettings = nil;
#else
        videoOutput.videoSettings =
            [NSDictionary dictionaryWithObjectsAndKeys:
                AVVideoCodecTypeH264, AVVideoCodecKey,
                nil, nil];
#endif

        delegate_ =
            [[MyVideoOutputDelegate alloc] initWithCallback:cb cookie:cookie];

        [videoOutput
            setSampleBufferDelegate:delegate_
                              queue:dispatchQueue_];

        [session_ addInput:videoInput];
        [session_ addOutput:videoOutput];

        [session_ commitConfiguration];
    }

    return self;
}

-(void)start {
    [session_ startRunning];
}

-(void)stop {
    [session_ stopRunning];
}

-(void)pause {
    [delegate_ pause];
}

-(void)resume {
    [delegate_ resume];
}

@end

void *createCameraSession(CameraSessionCallback cb, void *cookie) {
    return (void *)(CFBridgingRetain([[MyCameraSession alloc] initWithCallback:cb cookie:cookie]));
}

void startCameraSession(void *session) {
    [(__bridge MyCameraSession *)session start];
}

void stopCameraSession(void *session) {
    [(__bridge MyCameraSession *)session stop];
}

void pauseCameraSession(void *session) {
    [(__bridge MyCameraSession *)session pause];
}

void resumeCameraSession(void *session) {
    [(__bridge MyCameraSession *)session resume];
}

void destroyCameraSession(void *session) {
    CFBridgingRelease(session);
}


