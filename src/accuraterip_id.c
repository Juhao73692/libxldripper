// TOC ID algorithm adapted from XLDCueParser.m, XLD SVN r652.
// Created by tmkk on 06/06/10.
// Copyright 2006 tmkk. All rights reserved.
// Access to AccurateRip is regulated, see http://www.accuraterip.com/3rdparty-access.htm
// Modified for libxldripper. See README.md for attribution.

#include "internal.h"

void ripper_accuraterip_id(const xld_cdread_t *disc, xld_ripper_disc_info *info)
{
    int total = disc->numTracks;
    int audio = total;
    if (disc->tracks[total - 1].type == kTrackTypeData) {
        if (total < 2 || disc->numSessions != 2 || disc->tracks[total - 1].session != 2)
            return;
        --audio;
    } else if (disc->numSessions != 1) {
        return;
    }
    for (int i = 0; i < audio; ++i) {
        if (disc->tracks[i].type != kTrackTypeAudio || disc->tracks[i].session != 1)
            return;
    }
    uint32_t id1 = 0, id2 = 0, cddb = 0;
    for (int i = 0; i < total; ++i) {
        uint32_t offset = (uint32_t)disc->tracks[i].start / 588;
        if (i < audio) {
            id1 += offset;
            id2 += (offset ? offset : 1) * (uint32_t)(i + 1);
        }
        for (uint32_t n = offset / 75 + 2; n; n /= 10)
            cddb += n % 10;
    }
    uint32_t leadout = (uint32_t)(info->leadout_frame / 588);
    uint32_t duration = leadout / 75 - (uint32_t)disc->tracks[0].start / 588 / 75;
    info->accuraterip_tracks = (uint32_t)audio;
    info->accuraterip_id1 = id1 + leadout;
    info->accuraterip_id2 = id2 + leadout * (uint32_t)(audio + 1);
    info->accuraterip_cddb_id = ((cddb % 255) << 24) | (duration << 8) | (uint32_t)total;
}
