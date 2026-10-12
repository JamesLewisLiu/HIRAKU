/*
   Interlacing

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

   HIRAKU: ported from libretro slang-shaders crt/shaders/guest/fast/linearize.slang
   to GLSL 3.30 (see README.md).
*/

#pragma parameter bogus_gamma "[ GAMMA OPTIONS ]: " 0.0 0.0 0.0 1.0
#pragma parameter GAMMA_INPUT "          Gamma Input" 2.4 1.0 5.0 0.05
#pragma parameter gamma_out "          Gamma out" 2.4 1.0 5.0 0.05
#pragma parameter bogus_interlacing "[ INTERLACING OPTIONS ]: " 0.0 0.0 0.0 1.0
#pragma parameter inter "          Interlace Trigger Resolution/VGA Trigger:" 375.0 0.0 800.0 25.0
#pragma parameter interm "          Interlace Mode: OFF, Normal 1-3,6; Interpolation 4-5" 1.0 0.0 6.0 1.0
#pragma parameter iscan "          Interlacing Scanline Effect ('Laced brightness)" 0.20 0.0 1.0 0.05
#pragma parameter info02 "INFO --> Internal Resolution Y: 1.0 - Smart Scanline Mode" 0.0 0.0 1.0 1.0
#pragma parameter intres "          Internal Resolution Y: 0.5...y-dowsample divider" 0.0 0.0 10.0 0.5
#pragma parameter downsample_levelx "          Downsampling-X (High-res content, pre-scalers)" 0.0 0.0 2.0 0.05
#pragma parameter downsample_levely "          Downsampling-Y (High-res content, pre-scalers)" 0.0 0.0 2.0 0.05
#pragma parameter iscans "          Interlacing (Scanline) Saturation" 0.25 0.0 1.0 0.05
#pragma parameter vga_mode "          VGA Single/Double Scan mode" 0.0 0.0 1.0 1.0
#pragma parameter hiscan "          High Resolution Scanlines (prepend a scaler...)" 0.0 0.0 1.0 1.0

uniform float GAMMA_INPUT, inter, interm, iscan, intres, downsample_levelx, downsample_levely, iscans, vga_mode, hiscan;

uniform sampler2D PrePass;

vec3 fetch_pixel(vec2 coord)
{
	vec2 dx = vec2(SourceSize.z, 0.0) * downsample_levelx;
	vec2 dy = vec2(0.0, SourceSize.w) * downsample_levely;
	vec2 d1 = dx + dy;
	vec2 d2 = dx - dy;

	float sum = 15.0;
	vec3 result = 3.0*COMPAT_TEXTURE(PrePass, coord     ).rgb +
	              2.0*COMPAT_TEXTURE(PrePass, coord + dx).rgb +
	              2.0*COMPAT_TEXTURE(PrePass, coord - dx).rgb +
	              2.0*COMPAT_TEXTURE(PrePass, coord + dy).rgb +
	              2.0*COMPAT_TEXTURE(PrePass, coord - dy).rgb +
	              COMPAT_TEXTURE(PrePass, coord + d1).rgb +
	              COMPAT_TEXTURE(PrePass, coord - d1).rgb +
	              COMPAT_TEXTURE(PrePass, coord + d2).rgb +
	              COMPAT_TEXTURE(PrePass, coord - d2).rgb;

	return result/sum;
}


void main()
{
	vec3 c1 = COMPAT_TEXTURE(PrePass, vTexCoord).rgb;
	vec3 c2 = COMPAT_TEXTURE(PrePass, vTexCoord + vec2(0.0, OriginalSize.w)).rgb;

	if ((downsample_levelx + downsample_levely) > 0.025)
	{
		c1 = fetch_pixel(vTexCoord);
		c2 = fetch_pixel(vTexCoord + vec2(0.0, OriginalSize.w));
	}

	vec3  c  = c1;

	float intera = 1.0;
	float gamma_in = GAMMA_INPUT;

	float m1 = max(max(c1.r,c1.g),c1.b);
	float m2 = max(max(c2.r,c2.g),c2.b);
	vec3 df = abs(c1-c2);

	float d = max(max(df.r,df.g),df.b);
	if (interm == 2.0) d = mix(0.1*d,10.0*d, step(m1/(m2+0.0001),m2/(m1+0.0001)));

	float r = m1;

	float yres_div = 1.0; if (intres > 1.25) yres_div = intres;

	bool hscan = (hiscan > 0.5);

	if (inter <= OriginalSize.y/yres_div && interm > 0.5 && intres != 1.0  && intres != 0.5 && vga_mode < 0.5)
	{
		intera = 0.25;
		float line_no  = floor(mod(OriginalSize.y*vTexCoord.y, 2.0));
		float frame_no = floor(mod(float(FrameCount),2.0));
		float ii = abs(line_no-frame_no);

		if (interm < 3.5 || interm > 5.5)
		{
			if (interm == 6.0) c = mix(c2, c1, ii); else
			{
			c2 = plant(mix(c2, c2*c2, iscans), max(max(c2.r,c2.g),c2.b));
			r = max(m1*ii, (1.0-iscan)*min(m1,m2));
			c = plant( mix(mix(c1,c2, min(mix(m1, 1.0-m2, min(m1,1.0-m1))/(d+0.00001),1.0)), c1, ii), r);
			if (interm == 3.0) c = (1.0-0.5*iscan)*mix(c2, c1, ii);
			}
		}

		if (interm == 4.0) { c = plant(mix(c, c*c, 0.5*iscans), max(max(c.r,c.g),c.b)) * (1.0-0.5*iscan); }
		if (interm == 5.0) { c = mix(c2, c1, 0.5); c = plant(mix(c, c*c, 0.5*iscans), max(max(c.r,c.g),c.b)) * (1.0-0.5*iscan); }

		if (hscan) c = c1;
	}

	if (vga_mode > 0.5)
	{
		c = c1;
		if (inter <= OriginalSize.y) intera = 0.75; else intera = 0.5;
	}

	c = pow(c, vec3(gamma_in));

	if (vTexCoord.x > 0.5) gamma_in = intera; else gamma_in = 1.0/gamma_in;

	FragColor = vec4(c, gamma_in);
}
