//
//  XLDCDDARipper.m
//  XLD
//
//  Created by tmkk on 08/08/09.
//  Copyright 2008 tmkk. All rights reserved.
//
//  Access to AccurateRip is regulated, see  http://www.accuraterip.com/3rdparty-access.htm for details.

// Modified for libxldripper. See README.md for attribution.
#import "XLDCDDARipper.h"

@implementation XLDCDDARipper

- (instancetype)initWithDisc:(xld_cdread_t *)disc firstSector:(int)first
                  lastSector:(int)last mode:(XLDRipperMode)mode
                       retry:(int)retry offset:(int)offset
{
    self = [super init];
    if (!self) return nil;
    secureRipper = [[XLDSecureRipperEngine alloc] initWithReader:disc
                     firstSector:first lastSector:last];
    if (!secureRipper) {
        [self release];
        return nil;
    }
    [secureRipper setRipperMode:mode];
    [secureRipper setMaxRetryCount:retry];
    firstAudioFrame = first * 588;
    lastAudioFrame = (last + 1) * 588;
    offsetCorrectionValue = offset;
    return self;
}

- (void)dealloc
{
    [secureRipper release];
    [super dealloc];
}

- (void)seekToFrame:(int)frame
{
    // XLD applies the drive correction to the physical read position.
    position = frame + offsetCorrectionValue;
    int seekFrame = position < firstAudioFrame ? firstAudioFrame : position;
    [secureRipper seekToSector:seekFrame / 588];
    currentLSN = -1;
    sector = NULL;
}

// Like the original decoder's staging buffer, retain the unread part of a
// sector across calls. Stop at a sector or padding boundary for result reporting.
- (int)decodeToBuffer:(int16_t *)buffer frames:(int)count
               result:(XLDSecureRipperResult *)result sector:(int *)lsn
{
    *result = XLDSecureRipperNoErr;
    *lsn = -1;
    if (position < firstAudioFrame || position >= lastAudioFrame) {
        if (position < firstAudioFrame && count > firstAudioFrame - position)
            count = firstAudioFrame - position;
        memset(buffer, 0, (size_t)count * 4);
    } else {
        int physicalLSN = position / 588;
        int consumed = position % 588;
        if (currentLSN != physicalLSN) {
            *lsn = physicalLSN;
            sector = [secureRipper readSector:result];
            if (*result & (XLDSecureRipperReadErr | XLDSecureRipperNoMemory)) return 0;
            currentLSN = physicalLSN;
        }
        if (count > 588 - consumed) count = 588 - consumed;
        memcpy(buffer, sector + consumed * 2, (size_t)count * 4);
    }
    position += count;
    return count;
}

@end
