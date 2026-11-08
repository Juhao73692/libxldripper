#include <xld/ripper.h>

#include <stdio.h>
#include <stdlib.h>

static int write_pcm(const int16_t *pcm, size_t frames, void *context)
{
    FILE *output = context;
    return fwrite(pcm, XLD_RIPPER_BYTES_PER_FRAME, frames, output) != frames;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fputs("usage: rip_stdout /dev/diskN TRACK\n", stderr);
        return 2;
    }
    char *end;
    long track = strtol(argv[2], &end, 10);
    if (end == argv[2] || *end || track < 0 || track > 99) return 2;

    xld_ripper_session *session;
    xld_ripper_error error;
    xld_ripper_status status = xld_ripper_session_open(argv[1], &session, &error);
    if (status == XLD_RIPPER_OK) {
        xld_ripper_result result;
        status = xld_ripper_extract_track(session, (uint32_t)track, write_pcm, stdout, &result, &error);
        if (result.complete) fprintf(stderr, "CRC32 %08x\n", (unsigned)result.crc32);
        xld_ripper_session_close(session);
        if (fflush(stdout) && status == XLD_RIPPER_OK) return 1;
    }
    if (status != XLD_RIPPER_OK)
        fprintf(stderr, "%s (errno %d, LSN %d)\n", xld_ripper_status_string(status),
                (int)error.system_errno, (int)error.sector_lsn);
    return status != XLD_RIPPER_OK;
}
