#include <xld/ripper.h>

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t interrupted;

static void interrupt(int signal_number)
{
    interrupted = signal_number;
}

static void usage(FILE *file)
{
    fputs("Usage\n"
          "  xld-rip devices\n"
          "  xld-rip info [--device PATH] [--pregaps] [--probe-c2]\n"
          "  xld-rip rip --track N [--output FILE|-] [options]\n"
          "  xld-rip verify --track N [options]\n"
          "\n"
          "Options for rip and verify\n"
          "  --device PATH       Use a path listed by devices; default is the only CD media\n"
          "  --track N           TOC track number, or 0 for hidden track-one audio\n"
          "  --offset FRAMES     Signed drive correction in stereo frames\n"
          "  --retries N         Secure retry limit (default 20)\n"
          "  --burst             Use XLD Burst mode (default XLD Secure)\n"
          "  --c2                Require C2 pointer support in Secure mode\n"
          "  --speed X           Allow adaptive speed and set cap (0 for maximum)\n"
          "  --accuraterip FILE  Use caller-supplied dBAR records for this disc\n"
          "\n"
          "rip emits signed 16-bit LE stereo PCM at 44100 Hz (stdout by default).\n"
          "verify reads the same PCM and reports checksums without writing audio.\n"
          "  --help, --version\n", file);
}

static int report(const char *operation, xld_ripper_status status, const xld_ripper_error *detail)
{
    fprintf(stderr, "xld-rip: %s: %s", operation, xld_ripper_status_string(status));
    if (detail && detail->sector_lsn >= 0) fprintf(stderr, " (LSN %" PRId32 ")", detail->sector_lsn);
    if (detail && detail->system_errno) fprintf(stderr, ": %s", strerror(detail->system_errno));
    fputc('\n', stderr);
    return 1;
}

static int list_devices(char *selected)
{
    xld_ripper_device_list *list;
    xld_ripper_error error;
    xld_ripper_status status = xld_ripper_device_list_create(&list, &error);
    if (status != XLD_RIPPER_OK) return report("enumerate drives", status, &error);
    size_t media_count = 0;
    size_t count = xld_ripper_device_list_count(list);
    for (size_t i = 0; i < count; ++i) {
        xld_ripper_device_info info;
        if (xld_ripper_device_list_get(list, i, &info) != XLD_RIPPER_OK) continue;
        if (!selected) {
            printf("%016" PRIx64 "\t%s\t%s %s (firmware %s)\n",
                   info.registry_id, info.media_path[0] ? info.media_path : "[no CD media]",
                   info.vendor, info.product, info.firmware[0] ? info.firmware : "unknown");
        } else if (info.media_path[0]) {
            ++media_count;
            strcpy(selected, info.media_path);
        }
    }
    xld_ripper_device_list_free(list);
    if (selected && media_count != 1) {
        fputs(media_count ? "xld-rip: multiple CD media found; select --device\n" :
                            "xld-rip: no selectable CD media found\n", stderr);
        return 1;
    }
    if (!selected && !count) puts("No optical drives found.");
    return 0;
}

static int disc_info(xld_ripper_session *session, int pregaps, int c2)
{
    xld_ripper_disc_info disc;
    xld_ripper_disc_get_info(session, &disc);
    printf("tracks=%" PRIu32 " sessions=%" PRIu32 " leadout_frame=%" PRId64
           " htoa_frames=%" PRId64 "\n", disc.track_count, disc.session_count,
           disc.leadout_frame, disc.htoa_frames);
    if (disc.accuraterip_tracks)
        printf("dBAR-%03" PRIu32 "-%08" PRIx32 "-%08" PRIx32 "-%08" PRIx32 ".bin\n",
               disc.accuraterip_tracks, disc.accuraterip_id1,
               disc.accuraterip_id2, disc.accuraterip_cddb_id);
    int failed = 0;
    int previous_audio = 0;
    uint32_t previous_session = 0;
    for (uint32_t number = 1; number <= disc.track_count; ++number) {
        xld_ripper_track_info track;
        xld_ripper_track_get_info(session, number, &track);
        if (pregaps && track.is_audio && previous_audio && track.session == previous_session) {
            xld_ripper_error error;
            xld_ripper_status status = xld_ripper_track_probe_pregap(session, number, &error);
            if (status != XLD_RIPPER_OK) failed = report("scan pregap", status, &error);
            xld_ripper_track_get_info(session, number, &track);
        }
        printf("%02" PRIu32 " %s session=%" PRIu32 " index1=%" PRId64 " end=%" PRId64 " pregap=",
               number, track.is_audio ? "audio" : "data", track.session, track.index1_frame, track.end_frame);
        if (track.pregap_frames < 0) printf("unknown");
        else printf("%" PRId64, track.pregap_frames);
        printf(" pre_emphasis=%" PRIu32 " dcp=%" PRIu32 "\n", track.pre_emphasis,
               track.digital_copy_permitted);
        previous_audio = track.is_audio;
        previous_session = track.session;
    }
    if (c2) {
        uint32_t capability;
        xld_ripper_error error;
        xld_ripper_status status = xld_ripper_session_probe_c2(session, &capability, &error);
        printf("c2=%s\n", capability == XLD_RIPPER_C2_SUPPORTED ? "supported" :
                         capability == XLD_RIPPER_C2_UNSUPPORTED ? "unsupported" : "unknown");
        if (status != XLD_RIPPER_OK) failed = report("probe C2", status, &error);
    }
    return failed;
}

static int load_database(xld_ripper_session *session, const char *path)
{
    enum { LIMIT = 16 * 1024 * 1024 };
    FILE *file = fopen(path, "rb");
    if (!file) {
        perror(path);
        return 1;
    }
    unsigned char *data = malloc(LIMIT + 1u);
    if (!data) {
        fclose(file);
        return report("load dBAR", XLD_RIPPER_NO_MEMORY, NULL);
    }
    size_t size = fread(data, 1, LIMIT + 1u, file);
    int failed = ferror(file);
    fclose(file);
    xld_ripper_status status = failed ? XLD_RIPPER_IO :
        size == 0 || size > LIMIT ? XLD_RIPPER_INVALID_ACCURATERIP :
        xld_ripper_session_set_accuraterip_data(session, data, size);
    free(data);
    return status != XLD_RIPPER_OK ? report("load dBAR", status, NULL) : 0;
}

struct output {
    xld_ripper_session *session;
    FILE *file;
    int error;
};

static int consume_pcm(const int16_t *pcm, size_t frames, void *context)
{
    struct output *output = context;
    if (output->file && fwrite(pcm, XLD_RIPPER_BYTES_PER_FRAME, frames, output->file) != frames) {
        output->error = errno;
        return 1;
    }
    if (interrupted) xld_ripper_cancel(output->session);
    return 0;
}

static const char *ar_status(uint32_t status)
{
    static const char *const messages[] = {
        "not-applicable", "no-data", "track-not-found", "mismatch", "v1-match",
        "v2-match", "v1-v2-match", "v1-different-pressing", "v2-different-pressing",
        "v1-v2-different-pressing"
    };
    return status < sizeof(messages) / sizeof(*messages) ? messages[status] : "unknown";
}

static void print_result(FILE *file, const xld_ripper_result *result)
{
    const xld_ripper_progress *progress = &result->progress;
    fprintf(file, "track=%" PRIu32 " frames=%" PRIu64 "/%" PRIu64
                  " reads=%" PRIu64 " read_failures=%" PRIu64 " retry_sectors=%" PRIu32
                  " jitter=%" PRIu32 " damaged=%" PRIu32 "\n",
            progress->track, progress->frames_done, progress->frames_total, progress->read_attempts,
            progress->failed_attempts, progress->retried_sectors, progress->jitter_events,
            progress->damaged_sectors);
    if (result->speed_errno)
        fprintf(file, "speed_error=%s\n", strerror(result->speed_errno));
    if (!result->complete) return;
    fprintf(file, "crc32=%08" PRIx32 " crc32_skip_zero=%08" PRIx32 " padding_frames=%" PRIu64 "\n",
            result->crc32, result->crc32_skip_zero, result->padding_frames);
    fprintf(file, "accuraterip=%s ar_v1=%08" PRIx32 " ar_v2=%08" PRIx32
                  " confidence_v1=%" PRIu32 " confidence_v2=%" PRIu32 " submissions=%" PRIu32 "\n",
            ar_status(result->accuraterip_status), result->ar_v1, result->ar_v2,
            result->ar_v1_confidence, result->ar_v2_confidence, result->ar_total_submissions);
    if (result->ar_v1_pressing_offset || result->ar_v2_pressing_offset)
        fprintf(file, "pressing_v1_offset=%" PRId32 " pressing_v1_crc=%08" PRIx32
                      " pressing_v2_offset=%" PRId32 " pressing_v2_crc=%08" PRIx32 "\n",
                result->ar_v1_pressing_offset, result->ar_v1_pressing_crc,
                result->ar_v2_pressing_offset, result->ar_v2_pressing_crc);
}

static int extract(xld_ripper_session *session, uint32_t track, const char *path, int require_match)
{
    struct output output = {.session = session};
    char *temp = NULL;
    if (path && !strcmp(path, "-")) {
        output.file = stdout;
    } else if (path) {
        if (asprintf(&temp, "%s.xld-rip.XXXXXX", path) < 0)
            return report("create PCM output", XLD_RIPPER_NO_MEMORY, NULL);
        int fd = mkstemp(temp);
        if (fd >= 0) output.file = fdopen(fd, "wb");
        if (!output.file) {
            perror(path);
            if (fd >= 0) {
                close(fd);
                unlink(temp);
            }
            free(temp);
            return 1;
        }
    }

    struct sigaction action = {.sa_handler = interrupt};
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    signal(SIGPIPE, SIG_IGN);
    xld_ripper_result result;
    xld_ripper_error error;
    xld_ripper_status status = xld_ripper_extract_track(session, track, consume_pcm, &output, &result, &error);
    int output_ok = !output.error;
    if (output.file && (temp ? fclose(output.file) : fflush(output.file))) {
        output_ok = 0;
        if (!output.error) output.error = errno;
    }
    if (output.error) fprintf(stderr, "xld-rip: write PCM: %s\n", strerror(output.error));

    if (temp) {
        if (output_ok && !interrupted && (status == XLD_RIPPER_OK || status == XLD_RIPPER_DAMAGED) &&
            link(temp, path)) {
            perror(path);
            output_ok = 0;
        }
        if (unlink(temp)) {
            perror(temp);
            output_ok = 0;
        }
        free(temp);
    }
    print_result(path ? stderr : stdout, &result);
    if (status != XLD_RIPPER_OK) report("extract track", status, &error);
    if (interrupted) return 128 + interrupted;
    if (!output_ok || status != XLD_RIPPER_OK) return 1;
    if (!path && fflush(stdout)) {
        perror("xld-rip: verification output");
        return 1;
    }
    return require_match && result.accuraterip_status < XLD_RIPPER_AR_MATCH_V1 ? 2 : 0;
}

static int number(const char *text, long min, long max, long *value)
{
    char *end;
    errno = 0;
    long parsed = strtol(text, &end, 10);
    if (errno || end == text || *end || parsed < min || parsed > max) return 0;
    *value = parsed;
    return 1;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--help")) {
        usage(stdout);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--version")) {
        uint32_t abi = xld_ripper_api_version();
        printf("xld-rip %s (ABI %" PRIu32 ".%" PRIu32 ".%" PRIu32 ")\n",
               xld_ripper_version(), abi >> 16, (abi >> 8) & 0xffu, abi & 0xffu);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "devices")) return list_devices(NULL);
    if (argc < 2) {
        usage(stderr);
        return 2;
    }

    int info = !strcmp(argv[1], "info");
    int rip = !strcmp(argv[1], "rip");
    int verify = !strcmp(argv[1], "verify");
    if (!info && !rip && !verify) {
        usage(stderr);
        return 2;
    }
    const char *path = NULL, *output = rip ? "-" : NULL, *database = NULL;
    int pregaps = 0, c2 = 0;
    long track = -1;
    xld_ripper_options options;
    xld_ripper_options_init(&options);
    for (int i = 2; i < argc; ++i) {
        const char *arg = argv[i];
        long parsed;
        if (!strcmp(arg, "--device") && i + 1 < argc) path = argv[++i];
        else if (info && !strcmp(arg, "--pregaps")) pregaps = 1;
        else if (info && !strcmp(arg, "--probe-c2")) c2 = 1;
        else if (!info && !strcmp(arg, "--burst")) options.mode = XLD_RIPPER_BURST;
        else if (!info && !strcmp(arg, "--c2")) options.c2_policy = XLD_RIPPER_C2_REQUIRED;
        else if (rip && !strcmp(arg, "--output") && i + 1 < argc) output = argv[++i];
        else if (!info && !strcmp(arg, "--accuraterip") && i + 1 < argc) database = argv[++i];
        else if (!info && !strcmp(arg, "--track") && i + 1 < argc && number(argv[i + 1], 0, 99, &track)) ++i;
        else if (!info && !strcmp(arg, "--offset") && i + 1 < argc && number(argv[i + 1], -8192, 8192, &parsed)) {
            options.read_offset_frames = (int32_t)parsed;
            ++i;
        } else if (!info && !strcmp(arg, "--retries") && i + 1 < argc && number(argv[i + 1], 0, 100, &parsed)) {
            options.max_retries = (uint32_t)parsed;
            ++i;
        } else if (!info && !strcmp(arg, "--speed") && i + 1 < argc && number(argv[i + 1], 0, 52, &parsed)) {
            options.speed_control = 1;
            options.max_speed_x = (uint32_t)parsed;
            ++i;
        } else {
            fprintf(stderr, "xld-rip: invalid option or value: %s\n", arg);
            return 2;
        }
    }
    if (!info && track < 0) {
        fputs("xld-rip: --track is required\n", stderr);
        return 2;
    }
    if (options.mode == XLD_RIPPER_BURST && options.c2_policy) {
        fputs("xld-rip: --c2 requires Secure mode\n", stderr);
        return 2;
    }
    char selected[sizeof(((xld_ripper_device_info *)0)->media_path)];
    if (!path) {
        if (list_devices(selected)) return 1;
        path = selected;
    }

    xld_ripper_session *session;
    xld_ripper_error error;
    xld_ripper_status status = xld_ripper_session_open(path, &session, &error);
    if (status != XLD_RIPPER_OK) return report("open CD media", status, &error);
    int code;
    if (info) code = disc_info(session, pregaps, c2);
    else if ((status = xld_ripper_session_set_options(session, &options)) != XLD_RIPPER_OK)
        code = report("set options", status, NULL);
    else if (database && load_database(session, database)) code = 1;
    else code = extract(session, (uint32_t)track, output, verify && database);
    xld_ripper_session_close(session);
    return code;
}
