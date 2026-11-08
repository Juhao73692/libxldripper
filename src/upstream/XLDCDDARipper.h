//
//  XLDCDDARipper.h
//  XLD
//
//  Created by tmkk on 08/08/09.
//  Copyright 2008 tmkk. All rights reserved.
//
//  Access to AccurateRip is regulated, see  http://www.accuraterip.com/3rdparty-access.htm for details.

// Modified for libxldripper. See README.md for attribution.
#import "XLDSecureRipperEngine.h"

__attribute__((objc_runtime_name("LibXLDRipperCDDARipper")))
@interface XLDCDDARipper : NSObject {
    XLDSecureRipperEngine *secureRipper;
    int firstAudioFrame;
    int lastAudioFrame;
    int offsetCorrectionValue;
    int currentLSN;
    int position;
    const short *sector;
}
- (instancetype)initWithDisc:(xld_cdread_t *)disc firstSector:(int)first
                  lastSector:(int)last mode:(XLDRipperMode)mode
                       retry:(int)retry offset:(int)offset;
- (void)seekToFrame:(int)frame;
- (int)decodeToBuffer:(int16_t *)buffer frames:(int)count
               result:(XLDSecureRipperResult *)result sector:(int *)lsn;
@end
