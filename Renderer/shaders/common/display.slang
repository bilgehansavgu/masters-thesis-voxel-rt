// What every shading kernel does the same way: the background and the conversion for the screen.

#include "thesis/shared/ViewColors.h"

// The background behind the scene: a vertical gradient. `v` is 0 at the top of the image, 1 at the bottom.
public float3 backgroundColor(float v)
{
    return lerp(float3(THESIS_COLOR_SKY_TOP), float3(THESIS_COLOR_SKY_BOTTOM), v);
}

// Linear colour to the value stored in the RGBA8 image.
public float4 toDisplay(float3 color)
{
    return float4(pow(saturate(color), 1.0 / THESIS_DISPLAY_GAMMA), 1.0);
}
