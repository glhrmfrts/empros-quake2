Empros Quake II - v2023.1.1
===========================

This is the readme for the "Empros Quake II" source port, or empquake2 for short.

About
=====

Empros Quake II is an enhanced client for id Software's Quake
II with focus on offline and coop gameplay. It's a fork of Yamagi Quake II
with the addition of modern rendering effects for the OpenGL 3 renderer.
These are subtle effects which do not aim to make Quake II look
like a modern AAA game (e.g. no over the top bump-mapping),
but instead try to preserve and enhance the original feel of the game.

Current features include:

- Render scaling for retro look without changing the window resolution
- HDR rendering
- Post-processing effects: bloom, ambient occlusion, motion blur, dithering
- Support for lightmapped water surfaces
- Realtime shadow mapping for dlights (experimental)
- Qbism's QBSP map format support for extended limits

Every feature is optional and the renderer was optimized so if you want a vanilla experience
you can still have it with pretty good performance for big maps.

How to play
===========

- Create a new directory in a location of your preference
- Copy the "baseq2" folder from your Q2 installation into that directory
- Unzip the contents of the Empros Quake II zip file into that same directory
- Run "empquake2.exe"

New Cvars
=========

- r_renderscale => Controls the virtual resolution of the in-game viewport,
		   which gets divided by (r_renderscale + 1). Valid values are integers from 0 to 7.
		   Use this if you want a "retro" look but without the hassle of changing the real window resolution.

- r_hdr => Enable/disable HDR rendering, if you want the bloom effect, this must be set to 1.

- r_bloom => Enable/disable the Bloom effect, requires r_hdr to be set to 1.

- r_ssao => Enable/disable the Screen-space ambient occlusion effect
	    (WARNING: this feature is still in an "experimental" state, there may be glitches).

- r_dithering => Controls the intensity of the color dithering. Valid values are integers from 0 to 4.

- r_motionblur => Controls the intensity of the motion blur. Valid values are any decimal number from 0 to 2.

- r_shadowmap => Controls the quality of the realtime shadow mapping.
		 Valid values are 0 = off, 1 = low quality, 2 = high quality.
		 NOTE: this feature is still experimental and may present glitches.

New console commands
====================

- fog <r> <g> <b> <density> => Sets the client's fog.

- fog <density> => Sets the client's fog density without changing the color.

- foglerp <time> <r> <g> <b> <density> => Smoothly sets the client's fog over <time>.

- foglerp <time> <density> => Smoothly sets the client's fog density over <time> without changing the color.

Source code & License
=====================

Source code: https://github.com/glhrmfrts/empros-quake2

This code is built upon Yamagi Quake II, which itself is based
on Icculus Quake II, which itself is based on Quake II 3.21.
Empros Quake II is released under the terms of the GPL version 2. See the
LICENSE file for further information.