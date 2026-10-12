/*
   Gaussian blur - horizontal pass, dynamic range, resizable

   Copyright (C) 2020 - 2025 guest(r)

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

   HIRAKU: ported from libretro slang-shaders crt/shaders/guest/fast/bloom_horizontal.slang
   to GLSL 3.30 (see README.md).
*/

#pragma parameter bogus_bloom "[ BLOOM/HALATION/(GLOW) PASS SETTINGS ]:" 0.0 0.0 1.0 1.0
#pragma parameter FINE_BLOOM "          Fine Bloom/Halation Sampling" 1.0 -1.0 5.0 1.0
#pragma parameter SIZEHB "          Horizontal Bloom/Halation Radius" 3.0 1.0 50.0 1.0
#pragma parameter SIGMA_HB "          Horizontal Bloom/Halation Sigma" 0.75 0.25 15.0 0.025

uniform float FINE_BLOOM, SIZEHB, SIGMA_HB;

uniform sampler2D LinearizePass;

float gaussian(float x, float invsqrsigma)
{
	return exp(-x*x*invsqrsigma);
}

void main()
{
	float fine_bloom = (FINE_BLOOM > 0.5) ? FINE_BLOOM : mix(0.75, 0.5, -FINE_BLOOM);
	float invsqrsigma = 1.0/(2.0*SIGMA_HB*SIGMA_HB);

	vec4 SourceSize1 = OriginalSize * vec4(fine_bloom, fine_bloom, 1.0/fine_bloom, 1.0/fine_bloom);
	float f = fract(SourceSize1.x * vTexCoord.x);
	f = 0.5 - f;
	vec2 tex = floor(SourceSize1.xy * vTexCoord)*SourceSize1.zw + 0.5*SourceSize1.zw;
	vec4 color = vec4(0.0);
	vec2 dx  = vec2(SourceSize1.z, 0.0);

	float w;
	float wsum = 0.0;
	vec4 pixel;
	float n = -SIZEHB;

	do
	{
		pixel  = COMPAT_TEXTURE(LinearizePass, tex + n*dx);
		w = gaussian(n+f, invsqrsigma);
		pixel.a = max(max(pixel.r, pixel.g),pixel.b);
		pixel.a*=pixel.a*pixel.a;
		color  = color + w * pixel;
		wsum   = wsum + w;
		n = n + 1.0;

	} while (n <= SIZEHB);

	color = color / wsum;

	FragColor = vec4(color.rgb, pow(color.a, 0.333333));
}
