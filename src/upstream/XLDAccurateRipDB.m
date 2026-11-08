//
//  XLDAccurateRipDB.m
//  XLD
//
//  Created by tmkk on 08/08/17.
//  Copyright 2008 tmkk. All rights reserved.
//
//  Access to AccurateRip is regulated, see  http://www.accuraterip.com/3rdparty-access.htm for details.

// Modified for libxldripper. See README.md for attribution.
#import "XLDAccurateRipDB.h"
#include <xld/ripper.h>


@implementation XLDAccurateRipDB

- (id)init
{
	self = [super init];
	if (self) {
		database = [[NSMutableArray alloc] init];
		offsetDatabase = [[NSMutableArray alloc] init];
	}
	return self;
}

- (void)dealloc
{
	[database release];
	[offsetDatabase release];
	[super dealloc];
}

/*
 - 1st entry -
 00h: number of tracks
 01h: discid1 (4 bytes, LE)
 05h: discid2 (4 bytes, LE)
 09h: discid3 (4 bytes, LE)
 - 1st track -
 0dh: confidence
 0eh: AR CRC (4 bytes, LE)
 12h: offset finding CRC; CRC of 450th sector in the track (4 bytes, LE)
 - 2nd track -
  :
*/

static uint32_t little32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}

static BOOL matchesRecords(const uint8_t *ptr, size_t length, uint32_t tracks,
                           uint32_t id1, uint32_t id2, uint32_t cddb)
{
    if (!ptr || tracks < 1 || tracks > 99 || length > XLD_RIPPER_ACCURATERIP_MAX_BYTES) return NO;
    size_t recordSize = 13 + 9 * tracks;
    if (!length || length % recordSize) return NO;
    for (size_t n = 0; n < length; n += recordSize) {
        if (ptr[n] != tracks || little32(ptr + n + 1) != id1 ||
            little32(ptr + n + 5) != id2 || little32(ptr + n + 9) != cddb) return NO;
    }
    return YES;
}

xld_ripper_status xld_ripper_accuraterip_data_validate(const xld_ripper_disc_info *disc,
                                                      const void *data, size_t size)
{
    if (!disc || !data || !size) return XLD_RIPPER_INVALID_ARGUMENT;
    if (!disc->accuraterip_tracks) return XLD_RIPPER_UNSUPPORTED;
    return matchesRecords(data, size, disc->accuraterip_tracks, disc->accuraterip_id1,
                          disc->accuraterip_id2, disc->accuraterip_cddb_id) ?
        XLD_RIPPER_OK : XLD_RIPPER_INVALID_ACCURATERIP;
}

- (BOOL)parseBytes:(const uint8_t *)ptr length:(size_t)length
        trackCount:(uint32_t)tracks id1:(uint32_t)id1 id2:(uint32_t)id2 cddb:(uint32_t)cddb
{
	if (!matchesRecords(ptr, length, tracks, id1, id2, cddb)) return NO;
	int trackNum = (int)tracks;
	int i;
	for(i=0;i<trackNum;i++) {
		NSMutableDictionary *dic = [[NSMutableDictionary alloc] init];
		[dic setObject:[NSNumber numberWithInt:0] forKey:@"MaxConfidence"];
		[dic setObject:[NSNumber numberWithInt:0] forKey:@"TotalConfidence"];
		[database addObject:dic];
		[dic release];
		[offsetDatabase addObject:[NSMutableDictionary dictionary]];
	}

	size_t n = 0;
	while(n < length) {
		n+=13;
		for(i=0;i<trackNum;i++) {
			int confidence = *(ptr+n);
			n++;
			unsigned int crc32 = little32(ptr+n);
			if(confidence) [[database objectAtIndex:i] setObject:[NSNumber numberWithInt:confidence] forKey:[NSNumber numberWithUnsignedInt:crc32]];
			n+=4;
			crc32 = little32(ptr+n);
			if(crc32 && confidence) [[offsetDatabase objectAtIndex:i] setObject:[NSNumber numberWithInt:confidence] forKey:[NSNumber numberWithUnsignedInt:crc32]];
			n+=4;
			if([[[database objectAtIndex:i] objectForKey:@"MaxConfidence"] intValue] < confidence) {
				[[database objectAtIndex:i] setObject:[NSNumber numberWithInt:confidence] forKey:@"MaxConfidence"];
			}
			[[database objectAtIndex:i] setObject:[NSNumber numberWithInt:confidence+[[[database objectAtIndex:i] objectForKey:@"TotalConfidence"] intValue]] forKey:@"TotalConfidence"];
		}
	}
	//NSLog(@"%@", [database description]);
	return YES;
}

- (BOOL)hasValidDataForTrack:(int)track
{
	if(track < 1 || (NSUInteger)track > [database count]) return NO;
	if(![[[database objectAtIndex:track-1] objectForKey:@"MaxConfidence"] intValue]) return NO;
	else return YES;
}

- (int)isAccurateCRC:(unsigned int)crc forTrack:(int)track
{
	if(track < 1 || (NSUInteger)track > [database count]) return -1;
	
	id obj = [[database objectAtIndex:track-1] objectForKey:[NSNumber numberWithUnsignedInt:crc]];
	if(obj) return [obj intValue];
	else return -1;
}

- (int)isAccurateOffsetCRC:(unsigned int)crc forTrack:(int)track
{
	if(track < 1 || (NSUInteger)track > [offsetDatabase count]) return -1;
	
	id obj = [[offsetDatabase objectAtIndex:track-1] objectForKey:[NSNumber numberWithUnsignedInt:crc]];
	if(obj) return [obj intValue];
	else return -1;
}

- (int)totalSubmissionsForTrack:(int)track
{
	if(track < 1 || (NSUInteger)track > [database count]) return 0;
	return [[[database objectAtIndex:track-1] objectForKey:@"TotalConfidence"] intValue];
}

@end
