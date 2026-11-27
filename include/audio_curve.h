/* Kiwi's eight sound fade curves, GPL-2.0-or-later.
 * Matched to the reference evaluator at 0x484cd0, including endpoint clamps.
 */
#ifndef XSYSTEM4_AUDIO_CURVE_H
#define XSYSTEM4_AUDIO_CURVE_H
#include <math.h>

static inline float audio_fade_curve(int curve, float t)
{
	if (t <= 0) return 0;
	if (t >= 1) return 1;
	switch (curve) {
	case 1: return logf(1 - t) * (-1.0f / 7.0f);
	case 2: return 1 - powf(1 - t, 2);
	case 3: return 1 - (powf(100, 1 - t) - 1) * (1.0f / 99.0f);
	case 4: return 1 - sinf((1 - t) * 1.5707963705062866f);
	case 5: return 0.5f - 0.5f * sinf(t * 3.1415927410125732f + 1.5707963705062866f);
	case 6: return 1 - sqrtf(1 - t);
	case 7:
		if (t < 0.05f) return powf(t, 4) * 7000;
		if (t <= 0.12f) return (t - 0.05f) * 4.6f + 0.045f;
		return 1 - expf((t - 0.065f) * -5.614973068237305f) * (1 - t);
	default: return t;
	}
}
#endif
