#pragma once

// Decay time in seconds from the -5 and -25 dB crossings of the backward integral over the first unbroken run of filled bins,
// each bin's energy spread evenly across it, 0 when under two bins hold energy or both crossings share a bin
inline float schroeder_t20(const float* energy, int count, float bin_seconds)
{
	// The run ends at the first empty bin past the first filled one, where the SDK's own fit stops too
	int end = 0;
	while (end < count && !(energy[end] > 0.f))
		++end;
	while (end < count && energy[end] > 0.f)
		++end;
	double total = 0.0;
	int filled = 0;
	for (int i = 0; i < end; ++i)
	{
		total += energy[i];
		filled += energy[i] > 0.f;
	}
	if (filled < 2)
		return 0.f;
	const double levels[2] = { 0.31622776601683794 * total, 0.0031622776601683794 * total };
	double at[2] = {};
	int bin[2] = {};
	int found = 0;
	double rest = total;
	for (int i = 0; i < end && found < 2; ++i)
	{
		const double e = energy[i];
		for (; found < 2 && rest - e <= levels[found]; ++found)
		{
			at[found] = e > 0.0 ? i + (rest - levels[found]) / e : i;
			bin[found] = i;
		}
		rest -= e;
	}
	if (found < 2 || bin[0] == bin[1])
		return 0.f;
	return float(3.0 * (at[1] - at[0]) * bin_seconds);
}
