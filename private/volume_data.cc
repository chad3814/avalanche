/**
 * (c) Chad Walker, Chris Kirmse
 */

#include <math.h>

#include "../video_reader.h"
#include "../utils.h"

#include "utils.h"
#include "volume_data.h"

using namespace Avalanche;

enum {
    SILENT_DB = -91,
};

VolumeData::VolumeData() {
}

VolumeData::~VolumeData() {
}

void VolumeData::addSamples(float *pcm_samples, int num_samples) {
    for (int i = 0; i < num_samples; i++) {
        if (pcm_samples[i] < -1 || pcm_samples[i] > 1) {
            // this happens quite a bit fwiw
            //log(LOG_INFO, "invalid audio value %i %f\n", i, pcm_samples[i]);
            continue;
        }

        m_sum_squares += pcm_samples[i] * pcm_samples[i];
        m_total_samples++;

        if (pcm_samples[i] < m_lowest) {
            m_lowest = pcm_samples[i];
        }
        if (pcm_samples[i] > m_highest) {
            m_highest = pcm_samples[i];
        }
    }
}

void VolumeData::calculateResults(GetVolumeDataResult &result) {
    //printf("sum squares of samples is %f over %li samples\n", m_sum_squares, m_total_samples);

    // useful formulas
    // https://dosits.org/science/advanced-topics/introduction-to-signal-levels/
    // rms = sqrt((sum of squares of displacements) / (number of displacements))
    // decibels = 20 * log10(rms)
    // which is the same as 10 * log10((sum of squares of displacements) / (number of displacements))

    // we report back in average (mean) decibels, so we don't actually need to calculate rms

    double power = m_sum_squares / m_total_samples;
    //printf("power is %f\n", power);

    // if you need rms, here's how to calculate it:
    //double rms = sqrt(power);
    //printf("root-mean-square of samples is %f\n", rms);

    double mean_volume;
    if (power < 0.0000001) {
        mean_volume = SILENT_DB;
    } else {
        mean_volume = log10(power) * 10;
    }

    result.mean_volume = mean_volume;

    double max_displacement;
    if (-m_lowest < m_highest) {
        max_displacement = m_highest;
    } else {
        max_displacement = -m_lowest;
    }

    double max_volume;
    if (max_displacement * max_displacement < 0.0000001) {
        max_volume = SILENT_DB;
    } else {
        max_volume = log10(max_displacement) * 20;
    }
    result.max_volume = max_volume;
}
