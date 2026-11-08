//
//  XLDAccurateRipDB.h
//  XLD
//
//  Created by tmkk on 08/08/17.
//  Copyright 2008 tmkk. All rights reserved.
//
//  Access to AccurateRip is regulated, see  http://www.accuraterip.com/3rdparty-access.htm for details.

// Modified for libxldripper. See README.md for attribution.
#import <Foundation/Foundation.h>

__attribute__((objc_runtime_name("LibXLDRipperAccurateRipDB")))
@interface XLDAccurateRipDB : NSObject {
    NSMutableArray *database;
    NSMutableArray *offsetDatabase;
}
- (BOOL)parseBytes:(const uint8_t *)bytes length:(size_t)length
        trackCount:(uint32_t)tracks id1:(uint32_t)id1 id2:(uint32_t)id2 cddb:(uint32_t)cddb;
- (int)isAccurateCRC:(unsigned int)crc forTrack:(int)track;
- (int)isAccurateOffsetCRC:(unsigned int)crc forTrack:(int)track;
- (BOOL)hasValidDataForTrack:(int)track;
- (int)totalSubmissionsForTrack:(int)track;
@end
