/**
 * (c) Chad Walker, Chris Kirmse
 */

#pragma once

#include <memory>

namespace Avalanche {

struct GetVolumeDataResult;

struct VolumeData {
public:
    VolumeData();
    ~VolumeData();

    void addSamples(float *pcm_samples, int num_samples);

    void calculateResults(GetVolumeDataResult &result);

private:
    // to calculate the mean volume
    uint64_t m_total_samples = 0;
    double m_sum_squares = 0;

    // to calculate the max volume
    float m_lowest = 1;
    float m_highest = -1;

};

}
