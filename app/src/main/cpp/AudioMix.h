//
// Created by fgodt on 18-11-28.
// Copyright (c) 2018 fgodt <fgodt@hotmail.com>
//

#ifndef FGODT_AUDIOMIX_H
#define FGODT_AUDIOMIX_H

/**
 * mix two media file to one aac file,
 * output aac file will be sample_rate = 48000
 *                         channels = 1
 *
 * @param f1 first media file path
 * @param f1stime first media file mix start time,
 *        1 for 1 millisecond.
 * @param f1duration first media file mix duration,
 *        1 for 1 millisecond
 *        If f1duration = -1 will mix all first file
 * @param f2 Second media file path
 * @param f2stime  second media file mix start time,
 *        1 for 1 millisecond
 * @param f2duration second media file mix duration,
 *        1 for 1 millisecond,
 *        If f2duration = -1 will mix all second file
 * @param fmix mix file output path
 * @param totalDuration mix file duration,
 *        1 for 1 millisecond
 *        If totalDuration = -1
 *        totalduration = (f1stime+f1duration)>(f2stime+f2duration)?
 *                        (f1stime+f1duration):(f2stime+f2duration)
 * @return >= 0 on success
 */

int audio_mix(const char *f1, long f1stime, long f1duration,
              const char *f2, long f2stime, long f2duration,
              const char *fmix, long totalDuration);

#endif //FGODT_AUDIOMIX_H
