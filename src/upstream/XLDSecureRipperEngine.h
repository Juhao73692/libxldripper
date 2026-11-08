//
//  XLDSecureRipperEngine.h
//  XLD
//
//  Created by tmkk on 10/11/13.
//  Copyright 2010 tmkk. All rights reserved.
//

// Modified for libxldripper. See README.md for attribution.
#import <Foundation/Foundation.h>
#import "XLDCDDABackend.h"

// Modes used by these paths in XLD_Prefix.pch.
typedef enum {
    kRipperModeBurst = 0,
    kRipperModeC2 = 0x1,
    kRipperModeXLD = 0x4,
} XLDRipperMode;

typedef enum
{
	XLDSecureRipperNoErr = 0x0,
	XLDSecureRipperReadErr = 0x1,
	XLDSecureRipperJitterErr = 0x2,
	XLDSecureRipperRetry = 0x4,
	XLDSecureRipperDamaged = 0x8,
	XLDSecureRipperNoMemory = 0x10,
} XLDSecureRipperResult;


__attribute__((objc_runtime_name("LibXLDRipperSecureRipperEngine")))
@interface XLDSecureRipperEngine : NSObject {
	xld_cdread_t *cdread;
	int currentLSN;
	int firstLSN;
	int lastLSN;
	int bufferedSectorBegin;
	int bufferedSectorSize;
	unsigned char *firstReadBufRaw;
	unsigned char *firstReadBuf;
	unsigned char *secondReadBuf;
	unsigned char *overlapBuffer;
	unsigned char *zeroSector;
	BOOL *verified;
	int maxRetryCount;
	BOOL jitterDetect;
	BOOL overlapBufferIsValid;
	XLDRipperMode ripperMode;
}

- (id)initWithReader:(xld_cdread_t *)reader firstSector:(int)first lastSector:(int)last;
- (void *)readSector:(XLDSecureRipperResult *)result;
- (void)seekToSector:(int)sector;
- (void)setMaxRetryCount:(int)count;
- (void)setRipperMode:(XLDRipperMode)mode;

@end
