/* Side: Shared */
/* The colour scale of the work views: the ONLY place these numbers are written.
 *
 * Read by shade_work.slang (the image) and by resolve/ColorScale.h (the colour bar in the panel
 * and in screenshots), so the bar shows exactly the colours of the image.
 *
 * The colour map is viridis (van der Walt and Smith, the default map of matplotlib). Equal steps
 * of the count give equal steps of perceived colour, its lightness rises from start to end, and
 * it stays readable for the common kinds of colour blindness and in greyscale print.
 * It is evaluated as the degree-6 polynomial fit of the reference table by Matt Zucker
 * (https://www.shadertoy.com/view/WlfXRN): colour(t) = C0 + t*(C1 + t*(C2 + ... + t*C6)), t in [0, 1].
 * The result is display-encoded (sRGB), like the reference table. tests/TestColorScale.cpp holds
 * the fit to the table within 0.015.
 *
 * Like Bindings.h, this file may contain nothing but #defines and comments.
 */
#ifndef THESIS_SHARED_COLOR_SCALE_H
#define THESIS_SHARED_COLOR_SCALE_H

/* How a count becomes the position t along the scale (ShadeWorkParams.scaleKind). */
#define THESIS_COLOR_SCALE_LINEAR 0 /* t = count / maximum */
#define THESIS_COLOR_SCALE_LOG2 1   /* t = log2(count) / log2(maximum); count 1 is the start */

/* Viridis polynomial: coefficient k of red, green and blue. */
#define THESIS_VIRIDIS_C0 0.2777273272234177, 0.005407344544966578, 0.3340998053353061
#define THESIS_VIRIDIS_C1 0.1050930431085774, 1.404613529898575, 1.384590162594685
#define THESIS_VIRIDIS_C2 -0.3308618287255563, 0.214847559468213, 0.09509516302823659
#define THESIS_VIRIDIS_C3 -4.634230498983486, -5.799100973351585, -19.33244095627987
#define THESIS_VIRIDIS_C4 6.228269936347081, 14.17993336680509, 56.69055260068105
#define THESIS_VIRIDIS_C5 4.776384997670288, -13.74514537774601, -65.35303263337234
#define THESIS_VIRIDIS_C6 -5.435455855934631, 4.645852612178535, 26.3124352495832

#endif
