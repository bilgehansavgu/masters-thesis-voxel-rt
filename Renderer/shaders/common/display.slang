// What every shading kernel does the same way: the background and the conversion for the screen.

// The background behind the scene: a vertical gradient. `v` is 0 at the top of the image, 1 at the bottom.
public float3 backgroundColor(float v)
{
    return lerp(float3(0.55, 0.70, 0.90), float3(0.22, 0.27, 0.36), v);
}

// Linear colour to the value stored in the RGBA8 image (gamma 2.2).
public float4 toDisplay(float3 color)
{
    return float4(pow(saturate(color), 1.0 / 2.2), 1.0);
}
