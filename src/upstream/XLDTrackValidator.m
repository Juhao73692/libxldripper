//
//  XLDTrackValidator.m
//  XLD
//
//  Created by tmkk on 10/11/26.
//  Copyright 2010 tmkk. All rights reserved.
//
//  Access to AccurateRip is regulated, see  http://www.accuraterip.com/3rdparty-access.htm for details.

// Modified for libxldripper. See README.md for attribution.
#import "XLDTrackValidator.h"

static inline uint32_t calcAR2CRC(unsigned int sample, int multiplier)
{
	uint64_t temp = (uint64_t)sample * (uint64_t)multiplier;
	uint32_t lo = temp & 0xffffffff;
	uint32_t hi = (temp >> 32) & 0xffffffff;
	return lo + hi;
}

static void memcpyIntToShort(short *dst, int *src, int elements)
{
	int i;
	for(i=0;i<elements;i++) dst[i] = (src[i] >> 16)&0xffff;
}

@implementation XLDTrackValidator

- (id)init
{
	self = [super init];
	if(!self) return nil;
	unsigned int i,k;
	for(i = 0; i < 256; ++i){
		unsigned int value = i;
		for(k = 0; k < 8; ++k){
			if(value & 1)
				value = 0xEDB88320 ^ (value >> 1);
			else
				value >>= 1;
		}
		crc32Table[i] = value;
	}
	trackLength = -1;
	CRC = 0xffffffff;
	CRC2 = 0xffffffff;
	preTrackSamples = calloc(1,2352*4);
	postTrackSamples = calloc(1,2352*4);
	if(!preTrackSamples || !postTrackSamples) {
		[self release];
		return nil;
	}
	return self;
}

- (void)dealloc
{
	if(ARDB) [ARDB release];
	free(preTrackSamples);
	free(postTrackSamples);
	if(offsetDetectionSamples) free(offsetDetectionSamples);
	[super dealloc];
}

- (void)setTrackNumber:(int)track
{
	currentTrack = track;
}

- (void)setTrackLength:(xldoffset_t)length
{
	trackLength = length;
}

- (void)setIsFirstTrack:(BOOL)flag
{
	isFirstTrack = flag;
}

- (void)setIsLastTrack:(BOOL)flag
{
	isLastTrack = flag;
}

- (BOOL)setAccurateRipDB:(XLDAccurateRipDB *)db
{
	if(trackLength >= 455*588 && [db hasValidDataForTrack:currentTrack]) {
		offsetDetectionSamples = malloc(2352*455);
		if(!offsetDetectionSamples) return NO;
	}
	ARDB = [db retain];
	return YES;
}

- (BOOL)needsPostTrackSamples
{
	return !isLastTrack && modifiedOffsetCount > 0;
}

- (void)commitPreTrackSamples:(int *)buffer
{
	memcpyIntToShort(preTrackSamples,buffer,588*2*4);
}

- (void)commitPostTrackSamples:(int *)buffer
{
	memcpyIntToShort(postTrackSamples,buffer,588*2*4);
}

- (void)detectOffset
{
	//NSLog(@"offset detection start @ %d",currentFrame);
	int i,j;
	int firstFrame = 0;
	int lastFrame = trackLength-1;
	if(isFirstTrack) firstFrame += 5*588-1;
	if(isLastTrack) lastFrame -= 5*588;
	
	/* calc offset finding CRC and check */
	for(i=-4*588;i<4*588;i++) {
		unsigned int tempCRC = 0;
		for(j=0;j<588;j++) {
			unsigned int sample = ((uint32_t)(uint16_t)offsetDetectionSamples[(450*588+i+j)*2] & 0xffff) | ((uint32_t)(uint16_t)offsetDetectionSamples[(450*588+i+j)*2+1] << 16);
			tempCRC += sample * (j+1);
		}
		int ret = [ARDB isAccurateOffsetCRC:tempCRC forTrack:currentTrack];
		if(tempCRC && ret > 0) { /* seems to be a good offset */
			//NSLog(@"offset detected with CRC %08X:%d (%d)",tempCRC,i,ret);
			if(i!=0) {
				modifiedOffsetCount++;
				modifiedOffsetArray[modifiedOffsetCount-1] = i;
				modifiedCurrentFrameArray[modifiedOffsetCount-1] = 0;
				modifiedAR1CRCArray[modifiedOffsetCount-1] = 0;
				modifiedAR2CRCArray[modifiedOffsetCount-1] = 0;
			}
		}
		if(modifiedOffsetCount == MAX_DIFFERENT_PRESSINGS) break;
	}
	
	/* non-zero offset found */
	if(modifiedOffsetCount) {
		for(j=0;j<modifiedOffsetCount;j++) {
			int offset = modifiedOffsetArray[j];
			xldoffset_t modifiedCurrentFrame = 0;
			if(offset < 0) {
				for(i=offset;i<0;i++) {
					if(modifiedCurrentFrame >= firstFrame && modifiedCurrentFrame <= lastFrame) {
						unsigned int sample = ((uint32_t)(uint16_t)preTrackSamples[(588*4+i)*2] & 0xffff) | ((uint32_t)(uint16_t)preTrackSamples[(588*4+i)*2+1] << 16);
						modifiedAR1CRCArray[j] += sample * (modifiedCurrentFrame + 1);
						modifiedAR2CRCArray[j] += calcAR2CRC(sample, modifiedCurrentFrame+1);
					}
					modifiedCurrentFrame++;
				}
				for(i=0;i<currentFrame;i++) {
					if(modifiedCurrentFrame >= firstFrame && modifiedCurrentFrame <= lastFrame) {
						unsigned int sample = ((uint32_t)(uint16_t)offsetDetectionSamples[i*2] & 0xffff) | ((uint32_t)(uint16_t)offsetDetectionSamples[i*2+1] << 16);
						modifiedAR1CRCArray[j] += sample * (modifiedCurrentFrame + 1);
						modifiedAR2CRCArray[j] += calcAR2CRC(sample, modifiedCurrentFrame+1);
					}
					modifiedCurrentFrame++;
				}
			}
			else {
				for(i=offset;i<currentFrame;i++) {
					if(modifiedCurrentFrame >= firstFrame && modifiedCurrentFrame <= lastFrame) {
						unsigned int sample = ((uint32_t)(uint16_t)offsetDetectionSamples[i*2] & 0xffff) | ((uint32_t)(uint16_t)offsetDetectionSamples[i*2+1] << 16);
						modifiedAR1CRCArray[j] += sample * (modifiedCurrentFrame + 1);
						modifiedAR2CRCArray[j] += calcAR2CRC(sample, modifiedCurrentFrame+1);
					}
					modifiedCurrentFrame++;
				}
			}
			modifiedCurrentFrameArray[j] = modifiedCurrentFrame;
		}
	}
	//NSLog(@"offset detection end");
}

- (void)commitSamples:(int *)buffer length:(int)length
{
	int i,j;
	
	int firstFrame = 0;
	int lastFrame = trackLength-1;
	if(isFirstTrack) firstFrame += 5*588-1;
	if(isLastTrack) lastFrame -= 5*588;
	
	for(i=0;i<length;i++) {
		unsigned int sample = ((buffer[i*2]>>16) & 0xffff) | (buffer[i*2+1] & 0xffff0000); // MSB 16bit: right, LSB 16bit: left
		
		/* AccurateRip CRC... only for track analysis */
		if(currentTrack) {
			/* fill buffer if needed*/
			if(offsetDetectionSamples && currentFrame < 455*588) {
				offsetDetectionSamples[currentFrame*2] = buffer[i*2]>>16;
				offsetDetectionSamples[currentFrame*2+1] = buffer[i*2+1]>>16;
			}
			/* calc CRC */
			if(currentFrame >= firstFrame && currentFrame <= lastFrame) {
				AR1CRC += sample * (currentFrame+1);
				AR2CRC += calcAR2CRC(sample, currentFrame+1);
			}
			if(modifiedOffsetCount) {
				for(j=0;j<modifiedOffsetCount;j++) {
					if(modifiedCurrentFrameArray[j] && modifiedCurrentFrameArray[j] >= firstFrame && modifiedCurrentFrameArray[j] <= lastFrame) {
						modifiedAR1CRCArray[j] += sample * (modifiedCurrentFrameArray[j]+1);
						modifiedAR2CRCArray[j] += calcAR2CRC(sample, modifiedCurrentFrameArray[j]+1);
					}
					modifiedCurrentFrameArray[j]++;
				}
			}
		}
		
		/* CRC32... always */
		if(currentFrame >= 0) {
			CRC = (CRC >> 8) ^ crc32Table[(CRC ^ (sample)) & 0xFF];
			CRC = (CRC >> 8) ^ crc32Table[(CRC ^ (sample>>8)) & 0xFF];
			CRC = (CRC >> 8) ^ crc32Table[(CRC ^ (sample>>16)) & 0xFF];
			CRC = (CRC >> 8) ^ crc32Table[(CRC ^ (sample>>24)) & 0xFF];
			if(buffer[i*2] != 0) { // left sample is not zero
				CRC2 = (CRC2 >> 8) ^ crc32Table[(CRC2 ^ (sample)) & 0xFF];
				CRC2 = (CRC2 >> 8) ^ crc32Table[(CRC2 ^ (sample>>8)) & 0xFF];
			}
			if(buffer[i*2+1] != 0) { // right sample is not zero
				CRC2 = (CRC2 >> 8) ^ crc32Table[(CRC2 ^ (sample>>16)) & 0xFF];
				CRC2 = (CRC2 >> 8) ^ crc32Table[(CRC2 ^ (sample>>24)) & 0xFF];
			}
		}
		
		currentFrame++;
		
		/* offset detection */
		if(currentFrame == 455*588 && ARDB && offsetDetectionSamples) {
			[self detectOffset];
			free(offsetDetectionSamples);
			offsetDetectionSamples = NULL;
		}
	}
}

- (void)finalizeVerification
{
	if(finalized) return;
	if(currentFrame < trackLength) return;
	finalized = YES;
	if(!ARDB) {
		accurateRipStatus = XLDARStatusNotFound;
		return;
	}
	if(![ARDB hasValidDataForTrack:currentTrack]) {
		accurateRipStatus = XLDARStatusNotFound;
		return;
	}
	
	int lastFrame = trackLength-1;
	if(isLastTrack) lastFrame -= 5*588;
	
	accurateRipStatus = XLDARStatusMismatch;
	
	int ret;
	
	if(modifiedOffsetCount) {
		int i,j;
		for(i=0;i<modifiedOffsetCount;i++) {
			if(modifiedCurrentFrameArray[i] <= lastFrame) {
				for(j=0;modifiedCurrentFrameArray[i]<=lastFrame&&j<2352;modifiedCurrentFrameArray[i]++,j++) {
					unsigned int sample = ((uint32_t)(uint16_t)postTrackSamples[j*2] & 0xffff) | ((uint32_t)(uint16_t)postTrackSamples[j*2+1] << 16);
					modifiedAR1CRCArray[i] += sample * (modifiedCurrentFrameArray[i] + 1);
					modifiedAR2CRCArray[i] += calcAR2CRC(sample, modifiedCurrentFrameArray[i]+1);
				}
			}
			//NSLog(@"offset %d: %08X[%d](AR1),%08X[%d](AR2)",modifiedOffsetArray[i],modifiedAR1CRCArray[i],[ARDB isAccurateCRC:modifiedAR1CRCArray[i] forTrack:currentTrack],modifiedAR2CRCArray[i],[ARDB isAccurateCRC:modifiedAR2CRCArray[i] forTrack:currentTrack]);
			ret = [ARDB isAccurateCRC:modifiedAR1CRCArray[i] forTrack:currentTrack];
			if(ret > 0) {
				accurateRipStatus = XLDARStatusDifferentPressingMatch;
				if(AR1Confidence < ret) {
					AR1Confidence = ret;
					modifiedAR1CRC = modifiedAR1CRCArray[i];
					modifiedAR1Offset = modifiedOffsetArray[i];
				}
			}
#if 1
			ret = [ARDB isAccurateCRC:modifiedAR2CRCArray[i] forTrack:currentTrack];
			if(ret > 0) {
				accurateRipStatus = XLDARStatusVer2DifferentPressingMatch;
				if(AR2Confidence < ret) {
					AR2Confidence = ret;
					modifiedAR2CRC = modifiedAR2CRCArray[i];
					modifiedAR2Offset = modifiedOffsetArray[i];
				}

			}
#endif
		}
	}
	if(AR1Confidence && AR2Confidence && modifiedAR1Offset == modifiedAR2Offset) {
		accurateRipStatus = XLDARStatusBothVerDifferentPressingMatch;
	}
	else if(AR2Confidence) {
		accurateRipStatus = XLDARStatusVer2DifferentPressingMatch;
	}
	else if(AR1Confidence) {
		accurateRipStatus = XLDARStatusDifferentPressingMatch;
	}
	//NSLog(@"offset 0: %08X[%d](AR1),%08X[%d](AR2)",AR1CRC,[ARDB isAccurateCRC:AR1CRC forTrack:currentTrack],AR2CRC,[ARDB isAccurateCRC:AR2CRC forTrack:currentTrack]);
	ret = [ARDB isAccurateCRC:AR1CRC forTrack:currentTrack];
	if(ret > 0) {
		accurateRipStatus = XLDARStatusMatch;
		AR1Confidence = ret;
	}
#if 1
	ret = [ARDB isAccurateCRC:AR2CRC forTrack:currentTrack];
	if(ret > 0) {
		if(accurateRipStatus == XLDARStatusMatch)
			accurateRipStatus = XLDARStatusBothVerMatch;
		else
			accurateRipStatus = XLDARStatusVer2Match;
		AR2Confidence = ret;
	}
#endif
}

- (unsigned int)crc32
{
	return CRC^0xFFFFFFFF;
}

- (unsigned int)crc32EAC
{
	return CRC2^0xFFFFFFFF;
}

- (XLDARStatus)accurateRipStatus
{
	[self finalizeVerification];
	return accurateRipStatus;
}

- (unsigned int)AR1CRC
{
	[self finalizeVerification];
	return AR1CRC;
}

- (unsigned int)offsetModifiedAR1CRC
{
	[self finalizeVerification];
	return modifiedAR1CRC;
}

- (int)AR1Confidence
{
	[self finalizeVerification];
	return AR1Confidence;
}

- (unsigned int)AR2CRC
{
	[self finalizeVerification];
	return AR2CRC;
}

- (unsigned int)offsetModifiedAR2CRC
{
	[self finalizeVerification];
	return modifiedAR2CRC;
}

- (int)AR2Confidence
{
	[self finalizeVerification];
	return AR2Confidence;
}

- (int)modifiedAR1Offset
{
	[self finalizeVerification];
	return modifiedAR1Offset;
}

- (int)modifiedAR2Offset
{
	[self finalizeVerification];
	return modifiedAR2Offset;
}

- (int)totalARSubmissions
{
	return [ARDB totalSubmissionsForTrack:currentTrack];
}

@end
