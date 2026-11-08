//
//  XLDCDDABackend.h
//  XLD
//
//  Created by tmkk on 10/11/9.
//  Copyright 2010 tmkk. All rights reserved.
//

// Modified for libxldripper. See README.md for attribution.
#ifndef XLD_CDDA_BACKEND_H
#define XLD_CDDA_BACKEND_H

#include <IOKit/storage/IOCDMediaBSDClient.h>
#include <stdatomic.h>
#include <stddef.h>

typedef enum {
    kTrackTypeAudio,
    kTrackTypeData,
} XLDTrackType;

typedef struct {
    int start;
    int length;
    int session;
    int pregap_frames; /* -1 before a Q scan */
    XLDTrackType type;
    int preEmphasis;
    int dcp;
} xld_track_t;

typedef struct {
    int fd;
    int numTracks;
    int numSessions;
    xld_track_t *tracks;
    int nonBCD;
    int maxSpeed;
    int speed_control;
    int speed_errno;
    uint16_t original_speed;
    const atomic_bool *cancel;
    int last_errno;
    int failed_lsn;
    int failed_errno;
    uint64_t read_attempts;
    uint64_t failed_attempts;
#ifdef XLD_RIPPER_TEST_TRANSPORT
    /* Private transport override for out-of-tree validation builds. */
    int (*read_ioctl)(void *context, dk_cd_read_t *request);
    void *read_context;
#endif
} xld_cdread_t;

/* Open an already selected whole CD media device for read-only ioctls. */
int xld_cdda_open(xld_cdread_t *disc, const char *device);
void xld_cdda_close(xld_cdread_t *disc);
int xld_cdda_parse_toc(xld_cdread_t *disc, const void *bytes, size_t size);
int xld_cdda_read(xld_cdread_t *disc, void *buffer, int lsn, int count);
int xld_cdda_read_with_c2(xld_cdread_t *disc, void *buffer, int lsn, int count);
void xld_cdda_audio_bounds(const xld_cdread_t *disc, int track, int *first, int *last);
int xld_cdda_speed_set(xld_cdread_t *disc, int speed);
int xld_cdda_speed_begin(xld_cdread_t *disc, int max_speed);
int xld_cdda_speed_end(xld_cdread_t *disc);
int xld_cdda_read_pregap(xld_cdread_t *disc, int track);

#endif
