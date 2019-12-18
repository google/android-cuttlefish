#pragma once

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#import <VideoToolbox/VideoToolbox.h>

typedef void (*CameraSessionCallback)(
        void *cookie,
        ssize_t csdIndex,
        int64_t timeUs,
        const void *data,
        size_t size);

@interface MyVideoOutputDelegate
    : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate> {
    BOOL first_;

    CameraSessionCallback cb_;
    void *cookie_;
    BOOL paused_;

#ifdef TARGET_IOS
    VTCompressionSessionRef compressionSession_;
#endif
}

-(id)initWithCallback:(CameraSessionCallback)cb cookie:(void *)cookie;
-(void)pause;
-(void)resume;

@end

@interface MyCameraSession : NSObject {
    AVCaptureSession *session_;
    AVCaptureDevice *camera_;
    dispatch_queue_t dispatchQueue_;
    MyVideoOutputDelegate *delegate_;
}

-(id)initWithCallback:(CameraSessionCallback)cb cookie:(void *)cookie;
-(void)pause;
-(void)resume;

@end

