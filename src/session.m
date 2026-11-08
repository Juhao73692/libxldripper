#import "internal.h"
#import "upstream/XLDCDDARipper.h"
#import "upstream/XLDTrackValidator.h"

#include <errno.h>
#include <pthread.h>
#include <string.h>

#ifndef XLD_RIPPER_VERSION
#define XLD_RIPPER_VERSION "0.0.1"
#endif

enum { SECTOR_FRAMES = 588, AR_CONTEXT_FRAMES = 4 * 588, CHUNK_FRAMES = 4096 };

struct xld_ripper_session {
    xld_cdread_t disc;
    xld_ripper_disc_info info;
    xld_ripper_options options;
    xld_ripper_progress progress;
    XLDAccurateRipDB *database;
    pthread_mutex_t lock;
    atomic_bool cancelled;
    uint32_t c2;
};

uint32_t xld_ripper_api_version(void) { return XLD_RIPPER_API_VERSION; }
const char *xld_ripper_version(void) { return XLD_RIPPER_VERSION; }

const char *xld_ripper_status_string(xld_ripper_status status)
{
    static const char *const messages[] = {
        "success", "invalid argument", "out of memory", "CD media or device unavailable",
        "permission denied", "unsupported device capability or disc layout", "invalid CD TOC",
        "CD I/O failed", "session is busy", "extraction cancelled", "PCM output callback failed",
        "extraction contains unconfirmed sectors", "invalid or mismatched AccurateRip records"
    };
    if (status < 0 || (size_t)status >= sizeof(messages) / sizeof(*messages))
        return "unknown status";
    return messages[status];
}

xld_ripper_status ripper_error(xld_ripper_error *error, xld_ripper_status status, int cause, int lsn)
{
    if (error) *error = (xld_ripper_error){status, cause, lsn};
    return status;
}

xld_ripper_status ripper_io_status(int cause)
{
    switch (cause) {
    case ENOMEM: return XLD_RIPPER_NO_MEMORY;
    case EACCES:
    case EPERM: return XLD_RIPPER_PERMISSION;
    case ENODEV:
    case ENXIO:
    case ENOENT: return XLD_RIPPER_NO_DEVICE;
    case EBUSY: return XLD_RIPPER_BUSY;
    case ENOTSUP:
    case ENOTTY:
    case ENOSYS: return XLD_RIPPER_UNSUPPORTED;
    case ECANCELED: return XLD_RIPPER_CANCELLED;
    default: return XLD_RIPPER_IO;
    }
}

xld_ripper_status ripper_session_create(xld_cdread_t *disc, xld_ripper_session **out)
{
    xld_ripper_session *session = calloc(1, sizeof(*session));
    if (!session) return XLD_RIPPER_NO_MEMORY;
    int status = pthread_mutex_init(&session->lock, NULL);
    if (status) {
        free(session);
        return ripper_io_status(status);
    }
    atomic_init(&session->cancelled, false);
    session->disc = *disc;
    session->disc.cancel = &session->cancelled;
    const xld_track_t *last = &disc->tracks[disc->numTracks - 1];
    session->info.track_count = disc->numTracks;
    session->info.session_count = disc->numSessions;
    session->info.leadout_frame = (int64_t)last->start + last->length;
    if (disc->tracks[0].type == kTrackTypeAudio)
        session->info.htoa_frames = disc->tracks[0].start;
    ripper_accuraterip_id(disc, &session->info);
    session->progress.last_problem_lsn = -1;
    session->progress.last_damaged_lsn = -1;
    xld_ripper_options_init(&session->options);
    *out = session;
    return XLD_RIPPER_OK;
}

static int valid_media_path(const char *path)
{
    if (!path || strncmp(path, "/dev/", 5)) return 0;
    path += 5;
    if (*path == 'r') ++path;
    if (strncmp(path, "disk", 4)) return 0;
    path += 4;
    size_t digits = strspn(path, "0123456789");
    return digits > 0 && digits <= 10 && !path[digits];
}

xld_ripper_status xld_ripper_session_open(const char *path, xld_ripper_session **out,
                                        xld_ripper_error *error)
{
    if (out) *out = NULL;
    if (!out || !valid_media_path(path))
        return ripper_error(error, XLD_RIPPER_INVALID_ARGUMENT, 0, -1);
    xld_cdread_t disc;
    int cause = xld_cdda_open(&disc, path);
    if (cause)
        return ripper_error(error, cause == EBADMSG ? XLD_RIPPER_INVALID_TOC : ripper_io_status(cause),
                            cause, -1);
    xld_ripper_status status = ripper_session_create(&disc, out);
    if (status != XLD_RIPPER_OK) xld_cdda_close(&disc);
    return ripper_error(error, status, 0, -1);
}

xld_ripper_status xld_ripper_session_close(xld_ripper_session *session)
{
    if (!session) return XLD_RIPPER_INVALID_ARGUMENT;
    pthread_mutex_lock(&session->lock);
    if (session->progress.running) {
        pthread_mutex_unlock(&session->lock);
        return XLD_RIPPER_BUSY;
    }
    pthread_mutex_unlock(&session->lock);
    @autoreleasepool {
        [session->database release];
    }
    xld_cdda_close(&session->disc);
    pthread_mutex_destroy(&session->lock);
    free(session);
    return XLD_RIPPER_OK;
}

xld_ripper_status xld_ripper_disc_get_info(xld_ripper_session *session, xld_ripper_disc_info *info)
{
    if (!session || !info) return XLD_RIPPER_INVALID_ARGUMENT;
    *info = session->info;
    return XLD_RIPPER_OK;
}

xld_ripper_status xld_ripper_track_get_info(xld_ripper_session *session, uint32_t track,
                                           xld_ripper_track_info *info)
{
    if (!session || !info || track == 0 || track > session->info.track_count)
        return XLD_RIPPER_INVALID_ARGUMENT;
    pthread_mutex_lock(&session->lock);
    const xld_track_t *t = &session->disc.tracks[track - 1];
    *info = (xld_ripper_track_info){
        .number = track,
        .session = t->session,
        .is_audio = t->type == kTrackTypeAudio,
        .pre_emphasis = t->preEmphasis,
        .digital_copy_permitted = t->dcp,
        .index1_frame = t->start,
        .end_frame = (int64_t)t->start + t->length,
        .pregap_frames = t->pregap_frames,
    };
    pthread_mutex_unlock(&session->lock);
    return XLD_RIPPER_OK;
}

xld_ripper_status xld_ripper_track_probe_pregap(xld_ripper_session *session, uint32_t track,
                                               xld_ripper_error *error)
{
    if (!session || track == 0 || track > session->info.track_count)
        return ripper_error(error, XLD_RIPPER_INVALID_ARGUMENT, 0, -1);
    pthread_mutex_lock(&session->lock);
    xld_ripper_status status = XLD_RIPPER_BUSY;
    int cause = 0;
    int lsn = -1;
    if (!session->progress.running) {
        xld_cdread_t *disc = &session->disc;
        disc->failed_lsn = -1;
        if (disc->tracks[track - 1].type != kTrackTypeAudio) cause = ENOTSUP;
        else if (track > 1) cause = xld_cdda_read_pregap(disc, (int)track);
        status = cause ? ripper_io_status(cause) : XLD_RIPPER_OK;
        lsn = cause ? disc->failed_lsn : -1;
    }
    pthread_mutex_unlock(&session->lock);
    return ripper_error(error, status, cause, lsn);
}

/* Called under the session lock or by its active extractor. */
static xld_ripper_status probe_c2(xld_ripper_session *session, xld_ripper_error *error)
{
    if (session->c2 == XLD_RIPPER_C2_SUPPORTED) return ripper_error(error, XLD_RIPPER_OK, 0, -1);
    if (session->c2 == XLD_RIPPER_C2_UNSUPPORTED)
        return ripper_error(error, XLD_RIPPER_UNSUPPORTED, ENOTSUP, -1);

    xld_cdread_t *disc = &session->disc;
    for (int i = 0; i < disc->numTracks; ++i) {
        if (disc->tracks[i].type != kTrackTypeAudio) continue;
        int lsn = disc->tracks[i].start / SECTOR_FRAMES;
        uint8_t buffer[2352 + 294];
        if (xld_cdda_read_with_c2(disc, buffer, lsn, 1) == 1) {
            session->c2 = XLD_RIPPER_C2_SUPPORTED;
            return ripper_error(error, XLD_RIPPER_OK, 0, -1);
        }
        xld_ripper_status status = ripper_io_status(disc->last_errno);
        if (status == XLD_RIPPER_UNSUPPORTED) session->c2 = XLD_RIPPER_C2_UNSUPPORTED;
        return ripper_error(error, status, disc->last_errno, lsn);
    }
    return ripper_error(error, XLD_RIPPER_UNSUPPORTED, 0, -1);
}

xld_ripper_status xld_ripper_session_probe_c2(xld_ripper_session *session, uint32_t *capability,
                                             xld_ripper_error *error)
{
    if (!session || !capability) return ripper_error(error, XLD_RIPPER_INVALID_ARGUMENT, 0, -1);
    pthread_mutex_lock(&session->lock);
    xld_ripper_status status;
    if (session->progress.running) {
        *capability = XLD_RIPPER_C2_UNKNOWN;
        status = ripper_error(error, XLD_RIPPER_BUSY, 0, -1);
    } else {
        status = probe_c2(session, error);
        *capability = session->c2;
    }
    pthread_mutex_unlock(&session->lock);
    return status;
}

void xld_ripper_options_init(xld_ripper_options *options)
{
    if (options) *options = (xld_ripper_options){.max_retries = 20};
}

xld_ripper_status xld_ripper_session_set_options(xld_ripper_session *session,
                                                const xld_ripper_options *options)
{
    if (!session || !options || options->mode > XLD_RIPPER_BURST ||
        options->c2_policy > XLD_RIPPER_C2_REQUIRED ||
        (options->mode == XLD_RIPPER_BURST && options->c2_policy != XLD_RIPPER_C2_OFF) ||
        options->read_offset_frames < -8192 || options->read_offset_frames > 8192 ||
        options->max_retries > 100 || options->speed_control > 1 ||
        options->max_speed_x > 52 || (options->max_speed_x && !options->speed_control))
        return XLD_RIPPER_INVALID_ARGUMENT;
    pthread_mutex_lock(&session->lock);
    xld_ripper_status status = XLD_RIPPER_BUSY;
    if (!session->progress.running) {
        session->options = *options;
        status = XLD_RIPPER_OK;
    }
    pthread_mutex_unlock(&session->lock);
    return status;
}

xld_ripper_status xld_ripper_session_get_options(xld_ripper_session *session,
                                                xld_ripper_options *options)
{
    if (!session || !options) return XLD_RIPPER_INVALID_ARGUMENT;
    pthread_mutex_lock(&session->lock);
    *options = session->options;
    pthread_mutex_unlock(&session->lock);
    return XLD_RIPPER_OK;
}

xld_ripper_status xld_ripper_session_set_accuraterip_data(xld_ripper_session *session,
                                                        const void *data, size_t size)
{
    if (!session || (!data && size) || (data && !size)) return XLD_RIPPER_INVALID_ARGUMENT;
    pthread_mutex_lock(&session->lock);
    xld_ripper_status status = XLD_RIPPER_OK;
    @autoreleasepool {
        XLDAccurateRipDB *db = nil;
        const xld_ripper_disc_info *info = &session->info;
        if (session->progress.running) status = XLD_RIPPER_BUSY;
        else if (data && !info->accuraterip_tracks) status = XLD_RIPPER_UNSUPPORTED;
        else if (data) {
            db = [[XLDAccurateRipDB alloc] init];
            if (!db) status = XLD_RIPPER_NO_MEMORY;
            else if (![db parseBytes:data length:size trackCount:info->accuraterip_tracks
                                 id1:info->accuraterip_id1 id2:info->accuraterip_id2
                                cddb:info->accuraterip_cddb_id])
                status = XLD_RIPPER_INVALID_ACCURATERIP;
        }
        if (status == XLD_RIPPER_OK) {
            [session->database release];
            session->database = db;
        } else {
            [db release];
        }
    }
    pthread_mutex_unlock(&session->lock);
    return status;
}

void xld_ripper_cancel(xld_ripper_session *session)
{
    if (!session) return;
    pthread_mutex_lock(&session->lock);
    if (session->progress.running) atomic_store(&session->cancelled, true);
    pthread_mutex_unlock(&session->lock);
}

xld_ripper_status xld_ripper_get_progress(xld_ripper_session *session, xld_ripper_progress *progress)
{
    if (!session || !progress) return XLD_RIPPER_INVALID_ARGUMENT;
    pthread_mutex_lock(&session->lock);
    *progress = session->progress;
    pthread_mutex_unlock(&session->lock);
    return XLD_RIPPER_OK;
}

static void update_read_stats(const xld_cdread_t *disc, xld_ripper_progress *progress)
{
    if (progress->failed_attempts != disc->failed_attempts)
        progress->last_problem_lsn = disc->failed_lsn;
    progress->read_attempts = disc->read_attempts;
    progress->failed_attempts = disc->failed_attempts;
    progress->last_system_errno = disc->failed_attempts ? disc->failed_errno : 0;
}

static void publish_progress(xld_ripper_session *session, const xld_ripper_progress *progress)
{
    pthread_mutex_lock(&session->lock);
    session->progress = *progress;
    pthread_mutex_unlock(&session->lock);
}

static xld_ripper_status read_frames(xld_ripper_session *session, XLDCDDARipper *ripper,
                                    int16_t *buffer, int count, xld_ripper_progress *progress,
                                    xld_ripper_error *error)
{
    xld_cdread_t *disc = &session->disc;
    while (count) {
        if (atomic_load(&session->cancelled))
            return ripper_error(error, XLD_RIPPER_CANCELLED, 0, -1);
        XLDSecureRipperResult flags;
        int lsn;
        int got = [ripper decodeToBuffer:buffer frames:count result:&flags sector:&lsn];
        update_read_stats(disc, progress);
        if (flags & (XLDSecureRipperRetry | XLDSecureRipperDamaged)) ++progress->retried_sectors;
        if (flags & XLDSecureRipperDamaged) {
            ++progress->damaged_sectors;
            progress->last_damaged_lsn = lsn;
        }
        if (flags & XLDSecureRipperJitterErr) ++progress->jitter_events;
        if (flags & (XLDSecureRipperDamaged | XLDSecureRipperJitterErr))
            progress->last_problem_lsn = lsn;
        if (flags & XLDSecureRipperNoMemory)
            return ripper_error(error, XLD_RIPPER_NO_MEMORY, ENOMEM, lsn);
        if (flags & XLDSecureRipperReadErr)
            return ripper_error(error, ripper_io_status(disc->last_errno), disc->last_errno,
                                disc->last_errno == ECANCELED ? lsn : disc->failed_lsn);
        buffer += 2 * got;
        count -= got;
    }
    return XLD_RIPPER_OK;
}

static void align_samples(int *dest, const int16_t *pcm, int frames)
{
    for (int i = 0; i < frames * 2; ++i) dest[i] = (int)pcm[i] * 65536;
}

static void get_verification(XLDTrackValidator *validator, int use_ar, int has_db,
                             xld_ripper_result *result)
{
    static const uint32_t status_map[] = {
        [XLDARStatusNoQuery] = XLD_RIPPER_AR_NO_DATA,
        [XLDARStatusNotFound] = XLD_RIPPER_AR_TRACK_NOT_FOUND,
        [XLDARStatusMatch] = XLD_RIPPER_AR_MATCH_V1,
        [XLDARStatusDifferentPressingMatch] = XLD_RIPPER_AR_PRESSING_V1,
        [XLDARStatusMismatch] = XLD_RIPPER_AR_MISMATCH,
        [XLDARStatusVer2Match] = XLD_RIPPER_AR_MATCH_V2,
        [XLDARStatusVer2DifferentPressingMatch] = XLD_RIPPER_AR_PRESSING_V2,
        [XLDARStatusBothVerMatch] = XLD_RIPPER_AR_MATCH_BOTH,
        [XLDARStatusBothVerDifferentPressingMatch] = XLD_RIPPER_AR_PRESSING_BOTH,
    };
    result->crc32 = [validator crc32];
    result->crc32_skip_zero = [validator crc32EAC];
    if (!use_ar) return;
    result->ar_v1 = [validator AR1CRC];
    result->ar_v2 = [validator AR2CRC];
    result->accuraterip_status = XLD_RIPPER_AR_NO_DATA;
    if (!has_db) return;
    result->accuraterip_status = status_map[[validator accurateRipStatus]];
    result->ar_v1_confidence = [validator AR1Confidence];
    result->ar_v2_confidence = [validator AR2Confidence];
    result->ar_total_submissions = [validator totalARSubmissions];
    result->ar_v1_pressing_offset = [validator modifiedAR1Offset];
    result->ar_v2_pressing_offset = [validator modifiedAR2Offset];
    result->ar_v1_pressing_crc = [validator offsetModifiedAR1CRC];
    result->ar_v2_pressing_crc = [validator offsetModifiedAR2CRC];
}

static xld_ripper_status stream_track(xld_ripper_session *session, XLDCDDARipper *ripper,
                                     XLDTrackValidator *validator, uint32_t track, int start, int length,
                                     xld_ripper_pcm_callback callback, void *context,
                                     xld_ripper_progress *progress, xld_ripper_error *error)
{
    int16_t pcm[2 * CHUNK_FRAMES];
    int samples[2 * CHUNK_FRAMES];
    int pre = track > 1 && length >= 455 * SECTOR_FRAMES &&
              [session->database hasValidDataForTrack:(int)track] ? AR_CONTEXT_FRAMES : 0;
    [ripper seekToFrame:start - pre];
    if (pre) {
        xld_ripper_status status = read_frames(session, ripper, pcm, pre, progress, error);
        if (status != XLD_RIPPER_OK) return status;
        align_samples(samples, pcm, pre);
        [validator commitPreTrackSamples:samples];
    }

    for (int done = 0; done < length;) {
        int count = length - done;
        if (count > CHUNK_FRAMES) count = CHUNK_FRAMES;
        xld_ripper_status status = read_frames(session, ripper, pcm, count, progress, error);
        if (status != XLD_RIPPER_OK) return status;
        if (atomic_load(&session->cancelled))
            return ripper_error(error, XLD_RIPPER_CANCELLED, 0, -1);

        align_samples(samples, pcm, count);
        [validator commitSamples:samples length:count];
        if (callback && callback(pcm, count, context))
            return ripper_error(error, XLD_RIPPER_OUTPUT_ERROR, 0, -1);
        done += count;
        progress->frames_done = done;
        publish_progress(session, progress);
    }

    if ([validator needsPostTrackSamples]) {
        xld_ripper_status status = read_frames(session, ripper, pcm, AR_CONTEXT_FRAMES, progress, error);
        if (status != XLD_RIPPER_OK) return status;
        align_samples(samples, pcm, AR_CONTEXT_FRAMES);
        [validator commitPostTrackSamples:samples];
    }
    return XLD_RIPPER_OK;
}

xld_ripper_status xld_ripper_extract_track(xld_ripper_session *session, uint32_t track,
                                          xld_ripper_pcm_callback callback, void *context,
                                          xld_ripper_result *out, xld_ripper_error *error)
{
    if (out) *out = (xld_ripper_result){
        .progress = {.last_problem_lsn = -1, .last_damaged_lsn = -1}
    };
    if (!session || track > session->info.track_count)
        return ripper_error(error, XLD_RIPPER_INVALID_ARGUMENT, 0, -1);
    xld_cdread_t *disc = &session->disc;
    const xld_track_t *t = &disc->tracks[track ? track - 1 : 0];
    int length = track ? t->length : (int)session->info.htoa_frames;
    int start = track ? t->start : 0;
    if (t->type != kTrackTypeAudio || !length)
        return ripper_error(error, XLD_RIPPER_UNSUPPORTED, 0, -1);

    pthread_mutex_lock(&session->lock);
    if (session->progress.running) {
        pthread_mutex_unlock(&session->lock);
        return ripper_error(error, XLD_RIPPER_BUSY, 0, -1);
    }
    xld_ripper_progress progress = {
        .frames_total = length, .track = track, .running = 1, .last_problem_lsn = -1, .last_damaged_lsn = -1,
    };
    atomic_store(&session->cancelled, false);
    session->progress = progress;
    pthread_mutex_unlock(&session->lock);

    xld_ripper_result result = {0};
    xld_ripper_error detail = {XLD_RIPPER_OK, 0, -1};
    disc->last_errno = 0;
    disc->failed_lsn = -1;
    disc->read_attempts = disc->failed_attempts = 0;
    xld_ripper_options options = session->options;
    XLDRipperMode mode = options.mode == XLD_RIPPER_BURST ? kRipperModeBurst : kRipperModeXLD;
    if (options.c2_policy == XLD_RIPPER_C2_REQUIRED) {
        mode |= kRipperModeC2;
        probe_c2(session, &detail);
    }
    int speed_error = 0;
    if (detail.status == XLD_RIPPER_OK && options.speed_control && !atomic_load(&session->cancelled)) {
        speed_error = xld_cdda_speed_begin(disc, options.max_speed_x);
        if (speed_error) ripper_error(&detail, ripper_io_status(speed_error), speed_error, -1);
    }
    @autoreleasepool {
        if (detail.status == XLD_RIPPER_OK) {
            int first, last;
            xld_cdda_audio_bounds(disc, track ? (int)track : 1, &first, &last);
            XLDCDDARipper *ripper = [[XLDCDDARipper alloc] initWithDisc:disc firstSector:first
                lastSector:last mode:mode retry:options.max_retries offset:options.read_offset_frames];
            XLDTrackValidator *validator = [[XLDTrackValidator alloc] init];
            int use_ar = track > 0 && track <= session->info.accuraterip_tracks;
            [validator setTrackLength:length];
            [validator setTrackNumber:use_ar ? (int)track : 0];
            [validator setIsFirstTrack:track == 1];
            [validator setIsLastTrack:track == session->info.accuraterip_tracks];
            if (!ripper || ![validator setAccurateRipDB:use_ar ? session->database : nil]) {
                ripper_error(&detail, XLD_RIPPER_NO_MEMORY, ENOMEM, -1);
            } else if (stream_track(session, ripper, validator, track, start, length,
                                    callback, context, &progress, &detail) == XLD_RIPPER_OK) {
                int shifted = start + options.read_offset_frames;
                int overlap_start = MAX(first * SECTOR_FRAMES, shifted);
                int overlap_end = MIN((last + 1) * SECTOR_FRAMES, shifted + length);
                result.padding_frames = length - MAX(0, overlap_end - overlap_start);
                get_verification(validator, use_ar, session->database != nil, &result);
                result.complete = 1;
            }
            [validator release];
            [ripper release];
        }
    }
    int restore_error = xld_cdda_speed_end(disc);
    if (restore_error) speed_error = restore_error;
    if (speed_error && detail.status == XLD_RIPPER_OK)
        ripper_error(&detail, ripper_io_status(speed_error), speed_error, -1);
    update_read_stats(disc, &progress);

    pthread_mutex_lock(&session->lock);
    if (detail.status == XLD_RIPPER_OK && atomic_load(&session->cancelled)) {
        result = (xld_ripper_result){0};
        ripper_error(&detail, XLD_RIPPER_CANCELLED, 0, -1);
    }
    if (detail.status == XLD_RIPPER_OK && progress.damaged_sectors)
        ripper_error(&detail, XLD_RIPPER_DAMAGED, 0, progress.last_damaged_lsn);
    progress.running = 0;
    session->progress = progress;
    pthread_mutex_unlock(&session->lock);

    result.speed_errno = speed_error;
    result.progress = progress;
    if (out) *out = result;
    if (error) *error = detail;
    return detail.status;
}
