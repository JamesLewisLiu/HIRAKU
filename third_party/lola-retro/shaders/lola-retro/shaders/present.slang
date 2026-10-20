#version 450
// Lola Retro - pass 7: linear light -> display, with a little dither against banding.

#include "common.inc"

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;

void main()
{
    gl_Position = global.MVP * Position;
    vTexCoord = TexCoord;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;

void main()
{
    vec3 c = pow(max(textureLod(Source, vTexCoord, 0.0).rgb, 0.0), vec3(1.0 / 2.2));
    vec2 fc = vTexCoord * params.OutputSize.xy;
    c += (hash13(vec3(fc, 1.0)) - 0.5) / 255.0;
    FragColor = vec4(c, 1.0);
}
