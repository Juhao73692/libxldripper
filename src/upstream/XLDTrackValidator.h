//
//  XLDTrackValidator.h
//  XLD
//
//  Created by tmkk on 10/11/26.
//  Copyright 2010 tmkk. All rights reserved.
//
//  Access to AccurateRip is regulated, see  http://www.accuraterip.com/3rdparty-access.htm for details.

// Modified for libxldripper. See README.md for attribution.
#import <Foundation/Foundation.h>
#import "XLDAccurateRipDB.h"

typedef int64_t xldoffset_t;  // XLD_Prefix.pch
#define MAX_DIFFERENT_PRESSINGS 20

typedef enum
{
	XLDARStatusNoQuery = 0x0,
	XLDARStatusNotFound,
	XLDARStatusMatch,
	XLDARStatusDifferentPressingMatch,
	XLDARStatusMismatch,
	XLDARStatusVer2Match,
	XLDARStatusVer2DifferentPressingMatch,
	XLDARStatusBothVerMatch,
	XLDARStatusBothVerDifferentPressingMatch
} XLDARStatus;

__attribute__((objc_runtime_name("LibXLDRipperTrackValidator")))
@interface XLDTrackValidator : NSObject {
	int currentTrack;
	xldoffset_t currentFrame;
	xldoffset_t trackLength;
	BOOL isFirstTrack;
	BOOL isLastTrack;
	short *preTrackSamples; /* 4 sectors (2352*4 bytes) */
	short *postTrackSamples; /* 4 sectors (2352*4 bytes) */
	short *offsetDetectionSamples; /* 450+4 sectors (2352*454 bytes) */
	unsigned int crc32Table[256];
	unsigned int CRC;
	unsigned int CRC2; /* exclude zero samples */
	unsigned int AR1CRC;
	unsigned int AR2CRC;
	int modifiedOffsetCount;
	int modifiedOffsetArray[MAX_DIFFERENT_PRESSINGS];
	xldoffset_t modifiedCurrentFrameArray[MAX_DIFFERENT_PRESSINGS];
	unsigned int modifiedAR1CRCArray[MAX_DIFFERENT_PRESSINGS];
	unsigned int modifiedAR2CRCArray[MAX_DIFFERENT_PRESSINGS];
	XLDAccurateRipDB *ARDB;
	BOOL finalized;
	XLDARStatus accurateRipStatus;
	int AR1Confidence;
	int AR2Confidence;
	unsigned int modifiedAR1CRC;
	unsigned int modifiedAR2CRC;
	int modifiedAR1Offset;
	int modifiedAR2Offset;
}

- (void)setTrackNumber:(int)track;
- (void)setTrackLength:(xldoffset_t)length;
- (void)setIsFirstTrack:(BOOL)flag;
- (void)setIsLastTrack:(BOOL)flag;
- (BOOL)setAccurateRipDB:(XLDAccurateRipDB *)db;
- (void)commitPreTrackSamples:(int *)buffer;
- (void)commitPostTrackSamples:(int *)buffer;
- (void)commitSamples:(int *)buffer length:(int)length;
- (unsigned int)crc32;
- (unsigned int)crc32EAC;
- (XLDARStatus)accurateRipStatus;
- (unsigned int)AR1CRC;
- (unsigned int)offsetModifiedAR1CRC;
- (int)AR1Confidence;
- (unsigned int)AR2CRC;
- (unsigned int)offsetModifiedAR2CRC;
- (int)AR2Confidence;
- (int)totalARSubmissions;
- (BOOL)needsPostTrackSamples;
- (int)modifiedAR1Offset;
- (int)modifiedAR2Offset;

@end
