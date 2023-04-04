/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 * Copyright (C) 2016-2017 Daniel Gibson
 * Copyright (C) 2022 Guilherme Nemeth
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * Renderer fog setup
 *
 * =======================================================================
 */


#include "../ref_shared.h"
#include "header/local.h"
#include "header/HandmadeMath.h"

static hmm_vec4 fog_params;
static hmm_vec4 old_fog_params;
static float lerp_timer;
static float lerp_time;

void GL3_Fog_f(void)
{
	float r, g, b, d;
	switch (ri.Cmd_Argc()) {
	case 2: // density only
		sscanf(ri.Cmd_Argv(1), "%f", &d);
		fog_params.W = d;
		old_fog_params.W = d;
		break;
	case 5: // density + color
		sscanf(ri.Cmd_Argv(1), "%f", &d);
		sscanf(ri.Cmd_Argv(2), "%f", &r);
		sscanf(ri.Cmd_Argv(3), "%f", &g);
		sscanf(ri.Cmd_Argv(4), "%f", &b);
		fog_params = old_fog_params = HMM_Vec4(r, g, b, d);
		break;
	default:
		R_Printf(PRINT_ALL, "usage: fog <density> <red> <green> <blue>\n");
		R_Printf(PRINT_ALL, "       fog <density>\n");
		break;
	}

	R_Printf(PRINT_ALL, "density = %f\nred = %f\ngreen = %f\nblue = %f\n",
		fog_params.W,
		fog_params.R,
		fog_params.G,
		fog_params.B);
}

static float old_time;

void GL3_FogLerp_f(void)
{
	old_fog_params = fog_params;

	float time, r, g, b, d;
	switch (ri.Cmd_Argc()) {
	case 3: // density only
		sscanf(ri.Cmd_Argv(1), "%f", &time);
		sscanf(ri.Cmd_Argv(2), "%f", &d);
		fog_params.W = d;
		break;
	case 6: // density + color
		sscanf(ri.Cmd_Argv(1), "%f", &time);
		sscanf(ri.Cmd_Argv(2), "%f", &d);
		sscanf(ri.Cmd_Argv(3), "%f", &r);
		sscanf(ri.Cmd_Argv(4), "%f", &g);
		sscanf(ri.Cmd_Argv(5), "%f", &b);
		fog_params = HMM_Vec4(r, g, b, d);
		break;
	default:
		R_Printf(PRINT_ALL, "usage: foglerp <time> <density> <red> <green> <blue>\n");
		R_Printf(PRINT_ALL, "       fog <time> <density>\n");
		return;
	}

	old_time = gl3_newrefdef.time;
	lerp_time = time;
	lerp_timer = time;
}

void GL3_Fog_Set(float r, float g, float b, float d)
{
	fog_params = old_fog_params = HMM_Vec4(r, g, b, d);
}

void GL3_Fog_SetupFrame(void)
{
	const float fraction = (lerp_time > 0.0f) ? 1.0f - (lerp_timer / lerp_time) : 1.0f;

	const hmm_vec4 delta = HMM_SubtractVec4(fog_params, old_fog_params);
	const hmm_vec4 frame_fog_params = HMM_AddVec4(old_fog_params, HMM_MultiplyVec4f(delta, fraction));

	gl3state.uni3DData.fogParams = frame_fog_params;

	if (lerp_timer > 0.0f) {
		float dt = gl3_newrefdef.time - old_time;
		// R_Printf(PRINT_ALL, "fog fraction: %f, dt: %f\n", fraction, dt);
		lerp_timer -= dt;
	}
	else {
		lerp_timer = 0.0f;
	}

	old_time = gl3_newrefdef.time;
}