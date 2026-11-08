//
//  XLDCDDABackend.c
//  XLD
//
//  Created by tmkk on 10/11/9.
//  Copyright 2010 tmkk. All rights reserved.
//

// Modified for libxldripper. See README.md for attribution.
#include "XLDCDDABackend.h"

#include <IOKit/IOKitLib.h>
#include <IOKit/IOBSD.h>
#include <IOKit/storage/IOCDMedia.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int toc_frame(CDMSF msf)
{
    if (msf.second >= 60 || msf.frame >= 75 || (msf.minute == 0 && msf.second < 2))
        return -1;
    return 588 * (int)CDConvertMSFToLBA(msf);
}

int xld_cdda_parse_toc(xld_cdread_t *disc, const void *bytes, size_t size)
{
    const CDTOC *toc = bytes;
    if (!bytes || size < offsetof(CDTOC, descriptors)) return EINVAL;
    size_t reported = (size_t)OSSwapBigToHostInt16(toc->length) + sizeof(toc->length);
    if (reported > size || reported < offsetof(CDTOC, descriptors) ||
        (reported - offsetof(CDTOC, descriptors)) % sizeof(CDTOCDescriptor)) return EINVAL;
	int descCount = (int)((reported - offsetof(CDTOC, descriptors)) / sizeof(CDTOCDescriptor));
	int i;
	int nextStart = 0;
	disc->numTracks = 0;
	disc->numSessions = toc->sessionLast;
	for(i=0;i<descCount;i++) {
		int tno = toc->descriptors[i].point;
		int adr = toc->descriptors[i].adr;
		if (adr != 1 || toc->descriptors[i].session != toc->sessionLast) continue;
		if(tno == 0xa1) {
			disc->numTracks = toc->descriptors[i].p.minute;
		}
		else if(tno == 0xa2) {
			nextStart = toc_frame(toc->descriptors[i].p);
		}
	}
	if(!disc->numTracks) { //fallback
		for(i=0;i<descCount;i++) {
			int tno = toc->descriptors[i].point;
			int adr = toc->descriptors[i].adr;
			if(tno > 0 && tno < 100 && adr == 1)
				disc->numTracks++;
		}
	}
	if(disc->numTracks < 1 || disc->numTracks > 99 || nextStart <= 0 ||
	   disc->numSessions < 1 || disc->numSessions > 99 ||
	   toc->sessionFirst != 1) return EINVAL;
	disc->tracks = calloc((size_t)disc->numTracks, sizeof(xld_track_t));
	if(!disc->tracks) return ENOMEM;
	for(i=0;i<descCount;i++) {
		int tno = toc->descriptors[i].point;
		int adr = toc->descriptors[i].adr;
		if(tno > 0 && tno <= disc->numTracks && adr == 1) {
			disc->tracks[tno-1].start = toc_frame(toc->descriptors[i].p);
			disc->tracks[tno-1].pregap_frames = -1;
			disc->tracks[tno-1].session = toc->descriptors[i].session;
			disc->tracks[tno-1].type = toc->descriptors[i].control & 0x4 ? kTrackTypeData : kTrackTypeAudio;
			disc->tracks[tno-1].preEmphasis = disc->tracks[tno-1].type == kTrackTypeData ? 0 : (toc->descriptors[i].control & 0x1 ? 1 : 0);
			disc->tracks[tno-1].dcp = (toc->descriptors[i].control & 0x2) ? 1 : 0;
		}
	}
	for(i=disc->numTracks-1;i>=0;i--) {
		if(disc->tracks[i].session < 1 || disc->tracks[i].session > disc->numSessions ||
		   disc->tracks[i].start < 0 || nextStart <= disc->tracks[i].start ||
		   (i < disc->numTracks-1 && disc->tracks[i].session > disc->tracks[i+1].session))
			goto invalid;
		disc->tracks[i].length = nextStart - disc->tracks[i].start;
		nextStart = disc->tracks[i].start;
		if(i<disc->numTracks-1 && disc->tracks[i].session != disc->tracks[i+1].session) {
			/* first additional session : 11250 sectors lead-out/in + 150 sectors pregap */
			if(disc->tracks[i].session == 1) disc->tracks[i].length -= 588*11400;
			/* further additional sessions : 6750 sectors lead-out/in + 150 sectors pregap */
			else disc->tracks[i].length -= 588*6900;
		}
		if(disc->tracks[i].length <= 0) goto invalid;
	}
	disc->tracks[0].pregap_frames = disc->tracks[0].start;
	return 0;

invalid:
	free(disc->tracks);
	disc->tracks = NULL;
	return EINVAL;
}

static int xld_cdda_open_readonly(const char *device)
{
    const int flags = O_RDONLY | O_NONBLOCK | O_CLOEXEC;
    int fd = open(device, flags);
    if (fd >= 0 || errno != EBUSY || strncmp(device, "/dev/disk", sizeof("/dev/disk") - 1) != 0)
        return fd;

    // cddafs can hold the block device open while the raw CD device is readable.
    char raw[32];
    int length = snprintf(raw, sizeof(raw), "/dev/r%s", device + sizeof("/dev/") - 1);
    if (length < 0 || (size_t)length >= sizeof(raw)) {
        errno = EBUSY;
        return -1;
    }
    return open(raw, flags);
}

int xld_cdda_open(xld_cdread_t *disc, const char *device)
{
    memset(disc, 0, sizeof(*disc));
    disc->fd = -1;
    const char *name = strrchr(device, '/') + 1;
    if (name[0] == 'r') ++name;
    io_service_t media = IOServiceGetMatchingService(MACH_PORT_NULL,
        IOBSDNameMatching(MACH_PORT_NULL, 0, name));
    if (!media) return ENODEV;
    int status = ENOTSUP;
    if (IOObjectConformsTo(media, kIOCDMediaClass)) {
        CFTypeRef data = IORegistryEntryCreateCFProperty(media, CFSTR(kIOCDMediaTOCKey), NULL, 0);
        if (data && CFGetTypeID(data) == CFDataGetTypeID()) {
            disc->fd = open(device, O_RDONLY | O_CLOEXEC);
            if (disc->fd < 0) disc->fd = xld_cdda_open_readonly(device);
            status = disc->fd < 0 ? errno : xld_cdda_parse_toc(disc,
                CFDataGetBytePtr(data), (size_t)CFDataGetLength(data));
            if (disc->fd >= 0 && status == EINVAL) status = EBADMSG;
        } else {
            status = EBADMSG;
        }
        if (data) CFRelease(data);
    }
    if (status == 0) {
        // Reject a different registry entry if the BSD name was reused during open.
        io_service_t current = IOServiceGetMatchingService(MACH_PORT_NULL,
            IOBSDNameMatching(MACH_PORT_NULL, 0, name));
        uint64_t expected_id, current_id;
        if (!current || IORegistryEntryGetRegistryEntryID(media, &expected_id) != KERN_SUCCESS ||
            IORegistryEntryGetRegistryEntryID(current, &current_id) != KERN_SUCCESS ||
            expected_id != current_id) status = ENODEV;
        if (current) IOObjectRelease(current);
    }
    IOObjectRelease(media);
    if (status) {
        xld_cdda_close(disc);
        return status;
    }
    // File descriptor policy only. Drive speed is controlled separately.
    (void)fcntl(disc->fd, F_NOCACHE, 1);
    return 0;
}

void xld_cdda_close(xld_cdread_t *disc)
{
    free(disc->tracks);
    if (disc->fd >= 0) close(disc->fd);
}

static int read_cd(xld_cdread_t *disc, dk_cd_read_t *request, int lsn)
{
    if (disc->cancel && atomic_load(disc->cancel)) {
        disc->last_errno = ECANCELED;
        return -1;
    }
    uint32_t requested = request->bufferLength;
    ++disc->read_attempts;
#ifdef XLD_RIPPER_TEST_TRANSPORT
    int ret = disc->read_ioctl ? disc->read_ioctl(disc->read_context, request)
                               : ioctl(disc->fd, DKIOCCDREAD, request);
#else
    int ret = ioctl(disc->fd, DKIOCCDREAD, request);
#endif
    if (ret < 0 || request->bufferLength != requested) {
        disc->last_errno = ret < 0 ? errno : EIO;
        disc->failed_lsn = lsn;
        disc->failed_errno = disc->last_errno;
        ++disc->failed_attempts;
        return -1;
    }
    return 0;
}

static int read_sectors(xld_cdread_t *disc, void *buffer, int lsn, int count, int c2)
{
    int stride = c2 ? 2352 + 294 : 2352;
    dk_cd_read_t read = {0};
    read.sectorArea = kCDSectorAreaUser | (c2 ? kCDSectorAreaErrorFlags : 0);
    read.sectorType = kCDSectorTypeCDDA;
    read.offset = (uint64_t)lsn * stride;
    read.bufferLength = (uint32_t)(stride * count);
    read.buffer = buffer;
    // Callers clamp XLD's prefetch, overlap and cache reads to an audio run.
    if (read_cd(disc, &read, lsn) < 0) return -1;
    return count;
}

int xld_cdda_read(xld_cdread_t *disc, void *buffer, int lsn, int count)
{
    return read_sectors(disc, buffer, lsn, count, 0);
}

int xld_cdda_read_with_c2(xld_cdread_t *disc, void *buffer, int lsn, int count)
{
    return read_sectors(disc, buffer, lsn, count, 1);
}

int xld_cdda_speed_set(xld_cdread_t *disc, int speed)
{
    if (!disc->speed_control) return 0;
    uint16_t spd = speed > 0 ? speed * kCDSpeedMin :
        (disc->maxSpeed ? disc->maxSpeed * kCDSpeedMin : kCDSpeedMax);
    if (ioctl(disc->fd, DKIOCCDSETSPEED, &spd) == 0) return 0;
    disc->speed_errno = errno;
    return -1;
}

int xld_cdda_speed_begin(xld_cdread_t *disc, int max_speed)
{
    if (ioctl(disc->fd, DKIOCCDGETSPEED, &disc->original_speed) < 0) return errno;
    disc->speed_control = 1;
    disc->maxSpeed = max_speed;
    disc->speed_errno = 0;
    return xld_cdda_speed_set(disc, -1) ? disc->speed_errno : 0;
}

int xld_cdda_speed_end(xld_cdread_t *disc)
{
    if (!disc->speed_control) return 0;
    disc->speed_control = 0;
    if (ioctl(disc->fd, DKIOCCDSETSPEED, &disc->original_speed) < 0) return errno;
    return disc->speed_errno;
}

void xld_cdda_audio_bounds(const xld_cdread_t *disc, int track, int *first, int *last)
{
    int begin = track - 1;
    int end = track - 1;
    while (begin > 0 && disc->tracks[begin - 1].type == kTrackTypeAudio &&
           disc->tracks[begin - 1].session == disc->tracks[begin].session)
        --begin;
    while (end + 1 < disc->numTracks && disc->tracks[end + 1].type == kTrackTypeAudio &&
           disc->tracks[end + 1].session == disc->tracks[end].session)
        ++end;
    *first = begin == 0 ? 0 : disc->tracks[begin].start / 588;
    *last = (disc->tracks[end].start + disc->tracks[end].length) / 588 - 1;
}
#define CRCPOLY  0x1021U

static unsigned int calc_crc(int n, unsigned char c[])
{
    unsigned int j, r;
    int i;
	
    r = 0x0000U;
	for (i = 0; i < n; i++) {
		r ^= (unsigned short)c[i] << 8;
		for (j = 0; j < 8; j++) {
            if (r & 0x8000U) r = (r << 1) ^ CRCPOLY;
			else             r <<= 1;
		}
    }
    return ~r & 0xFFFFU;
}

int xld_cdda_read_pregap(xld_cdread_t *disc, int track)
{
	if(track<=1 || track>disc->numTracks || disc->tracks[track-1].type != kTrackTypeAudio || disc->tracks[track-2].type != kTrackTypeAudio ||
	   disc->tracks[track-1].session != disc->tracks[track-2].session) return ENOTSUP;
	int state = 0;
	int errorCount = 0;
	int totalErrorCount = 0;
	int pregapLength = 0;
	int success = 0;
	int firstQchannelRead = 1;
	const int readSize = 2352 + 16;
	int sectorsPerRead = 75;
	int currentSectorIndex = sectorsPerRead;
	unsigned char *buffer = malloc(readSize*sectorsPerRead);
	if(!buffer) return ENOMEM;
	int status = EIO;
	unsigned char *QAreaPtr = buffer;
	dk_cd_read_t cdread;
	memset(&cdread, 0, sizeof(cdread));
	cdread.sectorArea = kCDSectorAreaUser | kCDSectorAreaSubChannelQ;
	cdread.sectorType = kCDSectorTypeCDDA;
	cdread.buffer = buffer;
	/* -------
	 state
	 0 : not a pregap
	 1 : suspicious
	 2 : pregap
	------- */

	int beginOffset = 300; /* 4 sec */
	if(disc->tracks[track-2].length/588 <= 300) {
		beginOffset = disc->tracks[track-2].length/588 - 1;
		firstQchannelRead = 0;
	}
	cdread.offset = (disc->tracks[track-1].start/588-beginOffset)*readSize;
	int sectorsAvailable = 0;
	while(1) {
		if(currentSectorIndex >= sectorsAvailable) {
			int remaining = disc->tracks[track-1].start/588 - (int)(cdread.offset/readSize) + 1;
			sectorsAvailable = remaining < sectorsPerRead ? remaining : sectorsPerRead;
			cdread.bufferLength = readSize * sectorsAvailable;
			if(read_cd(disc, &cdread, (int)(cdread.offset/readSize)) < 0) {
				status = disc->last_errno;
				goto last;
			}
			currentSectorIndex = 0;
			QAreaPtr = buffer + readSize - 16;
		}
		{
			unsigned int adr = QAreaPtr[0] & 0xf;
			unsigned int crc = QAreaPtr[10]<<8 | QAreaPtr[11];
			if(disc->nonBCD && adr == 0x1) {
				QAreaPtr[1] = ((QAreaPtr[1]/10)<<4) | (QAreaPtr[1]%10);
				QAreaPtr[2] = ((QAreaPtr[2]/10)<<4) | (QAreaPtr[2]%10);
				QAreaPtr[3] = ((QAreaPtr[3]/10)<<4) | (QAreaPtr[3]%10);
				QAreaPtr[4] = ((QAreaPtr[4]/10)<<4) | (QAreaPtr[4]%10);
				QAreaPtr[5] = ((QAreaPtr[5]/10)<<4) | (QAreaPtr[5]%10);
				QAreaPtr[7] = ((QAreaPtr[7]/10)<<4) | (QAreaPtr[7]%10);
				QAreaPtr[8] = ((QAreaPtr[8]/10)<<4) | (QAreaPtr[8]%10);
				QAreaPtr[9] = ((QAreaPtr[9]/10)<<4) | (QAreaPtr[9]%10);
			}
			if(crc && crc == calc_crc(10, QAreaPtr)) {
				errorCount = 0;
				
				if(adr == 0x1) { // this sector has a pregap info
					if(firstQchannelRead) {
						if(QAreaPtr[2] == 0) { // we are already in the pregap area at the 1st read!
							beginOffset += 75; // begin with 1 more seconds before the current position
							if(disc->tracks[track-2].length/588 <= beginOffset) {
								beginOffset = disc->tracks[track-2].length/588 - 1;
								firstQchannelRead = 0;
							}
							cdread.offset = (disc->tracks[track-1].start/588-beginOffset)*readSize;
							currentSectorIndex = sectorsPerRead;
							continue;
						}
						else firstQchannelRead = 0;
					}
					if(state == 0) {
						if(QAreaPtr[2] == 0) state = 1;
					}
					else if(state == 1) {
						if(QAreaPtr[2] == 0) state = 2;
						else state = 0;
					}
				}
				success = 1;
			}
			else {
				if(adr == 1 && crc && !disc->nonBCD) {
					QAreaPtr[1] = ((QAreaPtr[1]/10)<<4) | (QAreaPtr[1]%10);
					QAreaPtr[2] = ((QAreaPtr[2]/10)<<4) | (QAreaPtr[2]%10);
					QAreaPtr[3] = ((QAreaPtr[3]/10)<<4) | (QAreaPtr[3]%10);
					QAreaPtr[4] = ((QAreaPtr[4]/10)<<4) | (QAreaPtr[4]%10);
					QAreaPtr[5] = ((QAreaPtr[5]/10)<<4) | (QAreaPtr[5]%10);
					QAreaPtr[7] = ((QAreaPtr[7]/10)<<4) | (QAreaPtr[7]%10);
					QAreaPtr[8] = ((QAreaPtr[8]/10)<<4) | (QAreaPtr[8]%10);
					QAreaPtr[9] = ((QAreaPtr[9]/10)<<4) | (QAreaPtr[9]%10);
					if(crc == calc_crc(10, QAreaPtr)) {
						//fprintf(stderr,"This drive seems to return non-BCD MSF, switching to non-BCD mode...\n");
						disc->nonBCD = 1;
						state = 0;
						errorCount = 0;
						totalErrorCount = 0;
						pregapLength = 0;
						success = 0;
						firstQchannelRead = 1;
						beginOffset = 300; /* 4 sec */
						if(disc->tracks[track-2].length/588 <= 300) {
							beginOffset = disc->tracks[track-2].length/588 - 1;
							firstQchannelRead = 0;
						}
						cdread.offset = (disc->tracks[track-1].start/588-beginOffset)*readSize;
						currentSectorIndex = sectorsPerRead;
						continue;
					}
				}
				if(!success) {
					totalErrorCount++;
					if(totalErrorCount == 100) {
						goto last;
					}
				}
				errorCount++;
				if(errorCount < 5) {
					currentSectorIndex = sectorsPerRead;
					continue;
				}
				else errorCount = 0;
			}
		}
		currentSectorIndex++;
		QAreaPtr += readSize;
		cdread.offset+=readSize;
		if(cdread.offset > (uint64_t)(disc->tracks[track-1].start/588)*readSize) {
			/* reached next track */
			break;
		}
		if(state > 0) pregapLength++;
		else pregapLength = 0;
	}
	
	if(success && !errorCount) {
		disc->tracks[track-1].pregap_frames = pregapLength * 588;
		status = 0;
	}
last:
	free(buffer);
	return status;
}

