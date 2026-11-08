#ifndef XLD_RIPPER_H
#define XLD_RIPPER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ABI 0.0.1 packed as (major << 16) | (minor << 8) | patch. */
#define XLD_RIPPER_API_VERSION 0x000001u
#define XLD_RIPPER_SAMPLE_RATE 44100u
#define XLD_RIPPER_CHANNELS 2u
#define XLD_RIPPER_FRAMES_PER_SECTOR 588u
#define XLD_RIPPER_BYTES_PER_FRAME 4u

typedef struct xld_ripper_device_list xld_ripper_device_list;
typedef struct xld_ripper_session xld_ripper_session;

/* Status and enum-valued fields are fixed-width for FFI. */
typedef int32_t xld_ripper_status;
enum {
    XLD_RIPPER_OK = 0,
    XLD_RIPPER_INVALID_ARGUMENT,
    XLD_RIPPER_NO_MEMORY,
    XLD_RIPPER_NO_DEVICE,
    XLD_RIPPER_PERMISSION,
    XLD_RIPPER_UNSUPPORTED,
    XLD_RIPPER_INVALID_TOC,
    XLD_RIPPER_IO,
    XLD_RIPPER_BUSY,
    XLD_RIPPER_CANCELLED,
    XLD_RIPPER_OUTPUT_ERROR,
    XLD_RIPPER_DAMAGED,
    XLD_RIPPER_INVALID_ACCURATERIP
};

typedef struct {
    xld_ripper_status status;
    int32_t system_errno; /* 0 if inapplicable */
    int32_t sector_lsn;   /* -1 if unknown */
} xld_ripper_error;

typedef struct {
    uint64_t registry_id;   /* IOKit registry entry ID for this snapshot */
    char media_path[64];    /* empty if no selectable CD media */
    char vendor[128];
    char product[128];
    char firmware[64];
} xld_ripper_device_info;

typedef struct {
    uint32_t track_count;        /* includes data tracks */
    uint32_t session_count;
    int64_t leadout_frame;       /* disc-relative, exclusive */
    int64_t htoa_frames;         /* INDEX 01 of an audio track 1, otherwise 0 */
    uint32_t accuraterip_tracks; /* 0 for unsupported disc layouts */
    uint32_t accuraterip_id1;
    uint32_t accuraterip_id2;
    uint32_t accuraterip_cddb_id;
} xld_ripper_disc_info;

typedef struct {
    uint32_t number;             /* TOC track number, starting at 1 */
    uint32_t session;
    uint32_t is_audio;
    uint32_t pre_emphasis;
    uint32_t digital_copy_permitted;
    int64_t index1_frame;
    int64_t end_frame;           /* exclusive, includes next track's pregap */
    int64_t pregap_frames;       /* -1 until known, 0 if known absent */
} xld_ripper_track_info;

enum { XLD_RIPPER_SECURE = 0, XLD_RIPPER_BURST = 1 };
enum { XLD_RIPPER_C2_OFF = 0, XLD_RIPPER_C2_REQUIRED = 1 };
enum { XLD_RIPPER_C2_UNKNOWN = 0, XLD_RIPPER_C2_SUPPORTED = 1, XLD_RIPPER_C2_UNSUPPORTED = 2 };

typedef struct {
    uint32_t mode;
    uint32_t c2_policy;
    int32_t read_offset_frames; /* added to the physical read position, [-8192, 8192] */
    uint32_t max_retries;      /* XLD engine retry limit, [0, 100] */
    uint32_t speed_control;    /* 1 enables XLD adaptive speed and cap */
    uint32_t max_speed_x;      /* 0 selects drive maximum, otherwise [1, 52] */
} xld_ripper_options;

typedef struct {
    uint64_t frames_done;       /* delivered to a successful callback or discarded */
    uint64_t frames_total;
    uint64_t read_attempts;     /* native read requests, including cache and AR reads */
    uint64_t failed_attempts;   /* native errors and short reads, including recovered */
    uint32_t retried_sectors;  /* XLD Retry or Damaged result, counted once per sector visit */
    uint32_t jitter_events;    /* XLD jitter flags */
    uint32_t damaged_sectors;  /* XLD selected best candidate without confirming it */
    int32_t last_problem_lsn;  /* -1 if none */
    int32_t last_damaged_lsn;  /* -1 if none */
    int32_t last_system_errno; /* most recent native read errno, even if recovered */
    uint32_t track;
    uint32_t running;
} xld_ripper_progress;

enum {
    XLD_RIPPER_AR_NOT_APPLICABLE = 0,
    XLD_RIPPER_AR_NO_DATA,
    XLD_RIPPER_AR_TRACK_NOT_FOUND,
    XLD_RIPPER_AR_MISMATCH,
    XLD_RIPPER_AR_MATCH_V1,
    XLD_RIPPER_AR_MATCH_V2,
    XLD_RIPPER_AR_MATCH_BOTH,
    XLD_RIPPER_AR_PRESSING_V1,
    XLD_RIPPER_AR_PRESSING_V2,
    XLD_RIPPER_AR_PRESSING_BOTH
};

typedef struct {
    uint32_t complete;         /* 1 if all PCM and checksums are available */
    uint32_t crc32;            /* CRC32 of the exact emitted little-endian PCM */
    uint32_t crc32_skip_zero;  /* XLD/EAC CRC excluding zero-valued channel samples */
    uint32_t accuraterip_status;
    uint32_t ar_v1;
    uint32_t ar_v2;
    uint32_t ar_v1_confidence;
    uint32_t ar_v2_confidence;
    uint32_t ar_total_submissions;
    int32_t ar_v1_pressing_offset;
    int32_t ar_v2_pressing_offset;
    uint32_t ar_v1_pressing_crc;
    uint32_t ar_v2_pressing_crc;
    int32_t speed_errno;      /* speed setup, adaptive control or restore failure */
    uint64_t padding_frames;   /* offset correction outside the contiguous audio run */
    xld_ripper_progress progress;
} xld_ripper_result;

/* Interleaved signed 16-bit little-endian stereo PCM at 44,100 Hz, left first.
 * The buffer is valid only during the synchronous callback on the caller's
 * thread. Return 0 to consume the chunk, nonzero to abort with OUTPUT_ERROR. */
typedef int (*xld_ripper_pcm_callback)(const int16_t *pcm, size_t frames, void *context);

#if defined(__GNUC__)
#pragma GCC visibility push(default)
#endif

uint32_t xld_ripper_api_version(void);
const char *xld_ripper_version(void);
const char *xld_ripper_status_string(xld_ripper_status status);

/* Snapshot and session handles are owned by the caller. Info structs are copies.
 * Error outputs may be NULL. Other output pointers are required unless specified.
 * A device snapshot can be read concurrently while its owner keeps it alive. */
xld_ripper_status xld_ripper_device_list_create(xld_ripper_device_list **list, xld_ripper_error *error);
size_t xld_ripper_device_list_count(const xld_ripper_device_list *list);
xld_ripper_status xld_ripper_device_list_get(const xld_ripper_device_list *list, size_t index,
                                            xld_ripper_device_info *info);
void xld_ripper_device_list_free(xld_ripper_device_list *list);

/* Open a current IOCDMedia path from the snapshot, e.g. /dev/disk4. */
xld_ripper_status xld_ripper_session_open(const char *media_path, xld_ripper_session **session,
                                        xld_ripper_error *error);
/* Close returns BUSY during extraction. The owner must coordinate the handle's
 * lifetime so that no other thread can begin a call while close is destroying it. */
xld_ripper_status xld_ripper_session_close(xld_ripper_session *session);
xld_ripper_status xld_ripper_disc_get_info(xld_ripper_session *session, xld_ripper_disc_info *info);
xld_ripper_status xld_ripper_track_get_info(xld_ripper_session *session, uint32_t track,
                                           xld_ripper_track_info *info);

/* Explicit read probes. Unsupported Q layouts/devices and failed reads are
 * reported, and a failed probe leaves any previously known pregap intact. */
xld_ripper_status xld_ripper_track_probe_pregap(xld_ripper_session *session, uint32_t track,
                                               xld_ripper_error *error);
xld_ripper_status xld_ripper_session_probe_c2(xld_ripper_session *session, uint32_t *capability,
                                             xld_ripper_error *error);
void xld_ripper_options_init(xld_ripper_options *options);
xld_ripper_status xld_ripper_session_set_options(xld_ripper_session *session,
                                                const xld_ripper_options *options);
xld_ripper_status xld_ripper_session_get_options(xld_ripper_session *session,
                                                xld_ripper_options *options);

#define XLD_RIPPER_ACCURATERIP_MAX_BYTES (16u * 1024u * 1024u)

/* Check a complete dBAR response against a disc snapshot. No session is needed. */
xld_ripper_status xld_ripper_accuraterip_data_validate(const xld_ripper_disc_info *disc,
                                                      const void *data, size_t size);

/* Copies and validates caller-supplied dBAR records (up to 16 MiB) for this TOC.
 * NULL with size 0 clears the current data. Invalid input preserves prior data. */
xld_ripper_status xld_ripper_session_set_accuraterip_data(xld_ripper_session *session,
                                                        const void *data, size_t size);

/* Synchronously extract the INDEX 01 interval or track 0 (HTOA). NULL callback
 * discards the PCM while calculating the same result. Result may be NULL.
 * Complete damaged rips return DAMAGED with complete=1 and usable diagnostics.
 * Incomplete rips leave complete=0. Once started, the result includes progress.
 * One extraction per session. While running, getters and cancel are available,
 * including from the callback. Mutating operations and nested extract return BUSY.
 * Cancel is cooperative and cannot interrupt an in-flight kernel I/O request. */
xld_ripper_status xld_ripper_extract_track(xld_ripper_session *session, uint32_t track,
                                          xld_ripper_pcm_callback callback, void *context,
                                          xld_ripper_result *result, xld_ripper_error *error);
void xld_ripper_cancel(xld_ripper_session *session);
xld_ripper_status xld_ripper_get_progress(xld_ripper_session *session, xld_ripper_progress *progress);

#if defined(__GNUC__)
#pragma GCC visibility pop
#endif

#ifdef __cplusplus
}
#endif
#endif
