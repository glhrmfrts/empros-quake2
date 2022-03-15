/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 * Copyright (C) 2016-2017 Daniel Gibson
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
 * Model loading and caching for OpenGL3. Includes the .bsp file format
 *
 * =======================================================================
 */

#include "header/local.h"

enum { MAX_MOD_KNOWN = 512 };

YQ2_ALIGNAS_TYPE(int) static byte mod_novis[MAX_MAP_LEAFS / 8];
gl3model_t mod_known[MAX_MOD_KNOWN];
static int mod_numknown;
static int mod_max = 0;
int registration_sequence;

//===============================================================================

static qboolean
Mod_HasFreeSpace(void)
{
	int		i, used;
	gl3model_t	*mod;

	used = 0;

	for (i=0, mod=mod_known ; i < mod_numknown ; i++, mod++)
	{
		if (!mod->name[0])
			continue;
		if (mod->registration_sequence == registration_sequence)
		{
			used ++;
		}
	}

	if (mod_max < used)
	{
		mod_max = used;
	}

	// should same size of free slots as currently used
	return (mod_numknown + mod_max) < MAX_MOD_KNOWN;
}

mleaf_t *
GL3_Mod_PointInLeaf(vec3_t p, gl3model_t *model)
{
	mnode_t *node;
	float d;
	cplane_t *plane;

	if (!model || !model->nodes)
	{
		ri.Sys_Error(ERR_DROP, "%s: bad model", __func__);
	}

	node = model->nodes;

	while (1)
	{
		if (node->contents != -1)
		{
			return (mleaf_t *)node;
		}

		plane = node->plane;
		d = DotProduct(p, plane->normal) - plane->dist;

		if (d > 0)
		{
			node = node->children[0];
		}
		else
		{
			node = node->children[1];
		}
	}

	return NULL; /* never reached */
}

const byte*
GL3_Mod_ClusterPVS(int cluster, const gl3model_t *model)
{
	if ((cluster == -1) || !model->vis)
	{
		return mod_novis;
	}

	return Mod_DecompressVis((byte *)model->vis +
			model->vis->bitofs[cluster][DVIS_PVS],
			(model->vis->numclusters + 7) >> 3);
}

void
GL3_Mod_Modellist_f(void)
{
	int i, total, used;
	gl3model_t *mod;
	qboolean freeup;

	total = 0;
	used = 0;
	R_Printf(PRINT_ALL, "Loaded models:\n");

	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		char *in_use = "";

		if (mod->registration_sequence == registration_sequence)
		{
			in_use = "*";
			used ++;
		}

		if (!mod->name[0])
		{
			continue;
		}

		R_Printf(PRINT_ALL, "%8i : %s %s\n",
			mod->extradatasize, mod->name, in_use);
		total += mod->extradatasize;
	}

	R_Printf(PRINT_ALL, "Total resident: %i\n", total);
	// update statistics
	freeup = Mod_HasFreeSpace();
	R_Printf(PRINT_ALL, "Used %d of %d models%s.\n", used, mod_max, freeup ? ", has free space" : "");
}

void
GL3_Mod_Init(void)
{
	mod_max = 0;
	memset(mod_novis, 0xff, sizeof(mod_novis));
}

static void
Mod_LoadLighting(gl3model_t *loadmodel, byte *mod_base, lump_t *l)
{
	if (!l->filelen)
	{
		loadmodel->lightdata = NULL;
		return;
	}

	loadmodel->lightdata = Hunk_Alloc(l->filelen);
	memcpy(loadmodel->lightdata, mod_base + l->fileofs, l->filelen);
}

static void
Mod_LoadVisibility(gl3model_t *loadmodel, byte *mod_base, lump_t *l)
{
	int i;

	if (!l->filelen)
	{
		loadmodel->vis = NULL;
		return;
	}

	loadmodel->vis = Hunk_Alloc(l->filelen);
	memcpy(loadmodel->vis, mod_base + l->fileofs, l->filelen);

	loadmodel->vis->numclusters = LittleLong(loadmodel->vis->numclusters);

	for (i = 0; i < loadmodel->vis->numclusters; i++)
	{
		loadmodel->vis->bitofs[i][0] = LittleLong(loadmodel->vis->bitofs[i][0]);
		loadmodel->vis->bitofs[i][1] = LittleLong(loadmodel->vis->bitofs[i][1]);
	}
}

static void
Mod_LoadVertexes(gl3model_t *loadmodel, byte *mod_base, lump_t *l)
{
	dvertex_t *in;
	mvertex_t *out;
	int i, count;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		ri.Sys_Error(ERR_DROP, "%s: funny lump size in %s",
				__func__, loadmodel->name);
	}

	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->vertexes = out;
	loadmodel->numvertexes = count;

	for (i = 0; i < count; i++, in++, out++)
	{
		out->position[0] = LittleFloat(in->point[0]);
		out->position[1] = LittleFloat(in->point[1]);
		out->position[2] = LittleFloat(in->point[2]);
	}
}

static void
Mod_LoadSubmodels(gl3model_t *loadmodel, byte *mod_base, lump_t *l)
{
	dmodel_t *in;
	gl3model_t *out;
	int i, j, count;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		ri.Sys_Error(ERR_DROP, "%s: funny lump size in %s",
				__func__, loadmodel->name);
	}

	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->submodels = out;
	loadmodel->numsubmodels = count;

	for (i = 0; i < count; i++, in++, out++)
	{
		if (i == 0)
		{
			// copy parent as template for first model
			memcpy(out, loadmodel, sizeof(*out));
		}
		else
		{
			// copy first as template for model
			memcpy(out, loadmodel->submodels, sizeof(*out));
		}

		Com_sprintf (out->name, sizeof(out->name), "*%d", i);

		for (j = 0; j < 3; j++)
		{
			/* spread the mins / maxs by a pixel */
			out->mins[j] = LittleFloat(in->mins[j]) - 1;
			out->maxs[j] = LittleFloat(in->maxs[j]) + 1;
			out->origin[j] = LittleFloat(in->origin[j]);
		}

		out->radius = Mod_RadiusFromBounds(out->mins, out->maxs);
		out->firstnode = LittleLong(in->headnode);
		out->firstmodelsurface = LittleLong(in->firstface);
		out->nummodelsurfaces = LittleLong(in->numfaces);
		// visleafs
		out->numleafs = 0;
		//  check limits
		if (out->firstnode >= loadmodel->numnodes)
		{
			ri.Sys_Error(ERR_DROP, "%s: Inline model %i has bad firstnode",
					__func__, i);
		}
	}
}

static void
Mod_LoadEdges(gl3model_t *loadmodel, byte *mod_base, lump_t *l)
{
	dedge_t *in;
	medge_t *out;
	int i, count;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		ri.Sys_Error(ERR_DROP, "%s: funny lump size in %s",
				__func__, loadmodel->name);
	}

	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc((count + 1) * sizeof(*out));

	loadmodel->edges = out;
	loadmodel->numedges = count;

	for (i = 0; i < count; i++, in++, out++)
	{
		out->v[0] = (unsigned short)LittleShort(in->v[0]);
		out->v[1] = (unsigned short)LittleShort(in->v[1]);
	}
}

static void
Mod_LoadTexinfo(gl3model_t *loadmodel, byte *mod_base, lump_t *l)
{
	texinfo_t *in;
	mtexinfo_t *out, *step;
	int i, j, count;
	char name[MAX_QPATH];
	int next;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		ri.Sys_Error(ERR_DROP, "%s: funny lump size in %s",
				__func__, loadmodel->name);
	}

	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->texinfo = out;
	loadmodel->numtexinfo = count;

	for (i = 0; i < count; i++, in++, out++)
	{
		for (j = 0; j < 4; j++)
		{
			out->vecs[0][j] = LittleFloat(in->vecs[0][j]);
			out->vecs[1][j] = LittleFloat(in->vecs[1][j]);
		}

		out->flags = LittleLong(in->flags);
		next = LittleLong(in->nexttexinfo);

		if (next > 0)
		{
			out->next = loadmodel->texinfo + next;
		}
		else
		{
			out->next = NULL;
		}

		Com_sprintf(name, sizeof(name), "textures/%s.wal", in->texture);

		out->image = GL3_FindImage(name, it_wall);

		if (!out->image || out->image == gl3_notexture)
		{
			Com_sprintf(name, sizeof(name), "textures/%s.m8", in->texture);
			out->image = GL3_FindImage(name, it_wall);
		}

		if (!out->image)
		{
			R_Printf(PRINT_ALL, "Couldn't load %s\n", name);
			out->image = gl3_notexture;
		}
	}

	/* count animation frames */
	for (i = 0; i < count; i++)
	{
		out = &loadmodel->texinfo[i];
		out->numframes = 1;

		for (step = out->next; step && step != out; step = step->next)
		{
			out->numframes++;
		}
	}
}

/*
 * Fills in s->texturemins[] and s->extents[]
 */
static void
Mod_CalcSurfaceExtents(gl3model_t *loadmodel, msurface_t *s)
{
	float mins[2], maxs[2], val;
	int i, j, e;
	mvertex_t *v;
	mtexinfo_t *tex;
	int bmins[2], bmaxs[2];

	mins[0] = mins[1] = 999999;
	maxs[0] = maxs[1] = -99999;

	tex = s->texinfo;

	for (i = 0; i < s->numedges; i++)
	{
		e = loadmodel->surfedges[s->firstedge + i];

		if (e >= 0)
		{
			v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
		}
		else
		{
			v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];
		}

		for (j = 0; j < 2; j++)
		{
			val = v->position[0] * tex->vecs[j][0] +
				  v->position[1] * tex->vecs[j][1] +
				  v->position[2] * tex->vecs[j][2] +
				  tex->vecs[j][3];

			if (val < mins[j])
			{
				mins[j] = val;
			}

			if (val > maxs[j])
			{
				maxs[j] = val;
			}
		}
	}

	for (i = 0; i < 2; i++)
	{
		bmins[i] = floor(mins[i] / 16);
		bmaxs[i] = ceil(maxs[i] / 16);

		s->texturemins[i] = bmins[i] * 16;
		s->extents[i] = (bmaxs[i] - bmins[i]) * 16;
	}
}

extern void
GL3_SubdivideSurface(msurface_t *fa, gl3model_t* loadmodel);

static int calcTexinfoAndFacesSize(byte *mod_base, const lump_t *fl, const lump_t *tl)
{
	dface_t* face_in = (void *)(mod_base + fl->fileofs);
	texinfo_t* texinfo_in = (void *)(mod_base + tl->fileofs);

	if (fl->filelen % sizeof(*face_in) || tl->filelen % sizeof(*texinfo_in))
	{
		// will error out when actually loading it
		return 0;
	}

	int ret = 0;

	int face_count = fl->filelen / sizeof(*face_in);
	int texinfo_count = tl->filelen / sizeof(*texinfo_in);

	{
		// out = Hunk_Alloc(count * sizeof(*out));
		int baseSize = face_count * sizeof(msurface_t);
		baseSize = (baseSize + 31) & ~31;
		ret += baseSize;

		int ti_size = texinfo_count * sizeof(mtexinfo_t);
		ti_size = (ti_size + 31) & ~31;
		ret += ti_size;
	}

	int numWarpFaces = 0;

	for (int surfnum = 0; surfnum < face_count; surfnum++, face_in++)
	{
		int numverts = LittleShort(face_in->numedges);
		int ti = LittleShort(face_in->texinfo);
		if ((ti < 0) || (ti >= texinfo_count))
		{
			return 0; // will error out
		}
		int texFlags = LittleLong(texinfo_in[ti].flags);

		/* set the drawing flags */
		if (texFlags & SURF_WARP)
		{
			if (numverts > 60)
				return 0; // will error out in R_SubdividePolygon()

			// GL3_SubdivideSurface(out, loadmodel); /* cut up polygon for warps */
			// for each (pot. recursive) call to R_SubdividePolygon():
			//   sizeof(glpoly_t) + ((numverts - 4) + 2) * sizeof(gl3_3D_vtx_t)

			// this is tricky, how much is allocated depends on the size of the surface
			// which we don't know (we'd need the vertices etc to know, but we can't load
			// those without allocating...)
			// so we just count warped faces and use a generous estimate below

			++numWarpFaces;
		}
		else
		{
			// GL3_LM_BuildPolygonFromSurface(out);
			// => poly = Hunk_Alloc(sizeof(glpoly_t) + (numverts - 4) * sizeof(gl3_3D_vtx_t));
			int polySize = sizeof(glpoly_t) + (numverts - 4) * sizeof(gl3_3D_vtx_t);
			polySize = (polySize + 31) & ~31;
			ret += polySize;
		}
	}

	// yeah, this is a bit hacky, but it looks like for each warped face
	// 256-55000 bytes are allocated (usually on the lower end),
	// so just assume 48k per face to be safe
	ret += numWarpFaces * 49152;
	ret += 1000000; // and 1MB extra just in case

	return ret;
}

static void
Mod_LoadFaces(gl3model_t *loadmodel, byte *mod_base, lump_t *l)
{
	dface_t *in;
	msurface_t *out;
	int i, count, surfnum;
	int planenum, side;
	int ti;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		ri.Sys_Error(ERR_DROP, "%s: funny lump size in %s",
				__func__, loadmodel->name);
	}

	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->surfaces = out;
	loadmodel->numsurfaces = count;

	GL3_LM_BeginBuildingLightmaps(loadmodel);

	for (surfnum = 0; surfnum < count; surfnum++, in++, out++)
	{
		out->firstedge = LittleLong(in->firstedge);
		out->numedges = LittleShort(in->numedges);
		out->flags = 0;
		out->polys = NULL;

		planenum = LittleShort(in->planenum);
		side = LittleShort(in->side);

		if (side)
		{
			out->flags |= SURF_PLANEBACK;
		}

		out->plane = loadmodel->planes + planenum;

		ti = LittleShort(in->texinfo);

		if ((ti < 0) || (ti >= loadmodel->numtexinfo))
		{
			ri.Sys_Error(ERR_DROP, "%s: bad texinfo number",
					__func__);
		}

		out->texinfo = loadmodel->texinfo + ti;

		Mod_CalcSurfaceExtents(loadmodel, out);

		/* lighting info */
		for (i = 0; i < MAX_LIGHTMAPS_PER_SURFACE; i++)
		{
			out->styles[i] = in->styles[i];
		}

		i = LittleLong(in->lightofs);

		if (i == -1)
		{
			out->samples = NULL;
		}
		else
		{
			out->samples = loadmodel->lightdata + i;
		}

		/* set the drawing flags */
		if (out->texinfo->flags & SURF_WARP)
		{
			if (in->lightofs > 0)
			{
				out->flags |= SURF_DRAWTURBLIT;
			}
			else
			{
				out->flags |= SURF_DRAWTURB;
				for (i = 0; i < 2; i++)
				{
					out->extents[i] = 16384;
					out->texturemins[i] = -8192;
				}
				GL3_SubdivideSurface(out, loadmodel); /* cut up polygon for warps */
			}
		}

		if (r_fixsurfsky->value)
		{
			if (out->texinfo->flags & SURF_SKY)
			{
				out->flags |= SURF_DRAWSKY;
			}
		}

		/* create lightmaps and polygons */
		GL3_LM_CreateSurfaceLightmap(out);

		if ((out->flags & SURF_DRAWTURB))
		{
			GL3_LM_BuildPolygonFromWarpSurface(loadmodel, out);
		}
		else
		{
			GL3_LM_BuildPolygonFromSurface(loadmodel, out);
		}
	}

	GL3_LM_EndBuildingLightmaps();
}

static void
Mod_SetParent(mnode_t *node, mnode_t *parent)
{
	node->parent = parent;

	if (node->contents != -1)
	{
		return;
	}

	Mod_SetParent(node->children[0], node);
	Mod_SetParent(node->children[1], node);
}

static void
Mod_LoadNodes(gl3model_t *loadmodel, byte *mod_base, lump_t *l)
{
	int i, j, count, p;
	dnode_t *in;
	mnode_t *out;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		ri.Sys_Error(ERR_DROP, "%s: funny lump size in %s",
				__func__, loadmodel->name);
	}

	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->nodes = out;
	loadmodel->numnodes = count;

	for (i = 0; i < count; i++, in++, out++)
	{
		for (j = 0; j < 3; j++)
		{
			out->minmaxs[j] = LittleShort(in->mins[j]);
			out->minmaxs[3 + j] = LittleShort(in->maxs[j]);
		}

		p = LittleLong(in->planenum);
		out->plane = loadmodel->planes + p;

		out->firstsurface = LittleShort(in->firstface);
		out->numsurfaces = LittleShort(in->numfaces);
		out->contents = -1; /* differentiate from leafs */

		for (j = 0; j < 2; j++)
		{
			p = LittleLong(in->children[j]);

			if (p >= 0)
			{
				out->children[j] = loadmodel->nodes + p;
			}
			else
			{
				out->children[j] = (mnode_t *)(loadmodel->leafs + (-1 - p));
			}
		}
	}

	Mod_SetParent(loadmodel->nodes, NULL); /* sets nodes and leafs */
}

static void
Mod_LoadLeafs(gl3model_t *loadmodel, byte *mod_base, lump_t *l)
{
	dleaf_t *in;
	mleaf_t *out;
	int i, j, count, p;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		ri.Sys_Error(ERR_DROP, "%s: funny lump size in %s",
				__func__, loadmodel->name);
	}

	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	for (i = 0; i < count; i++, in++, out++)
	{
		unsigned firstleafface;

		for (j = 0; j < 3; j++)
		{
			out->minmaxs[j] = LittleShort(in->mins[j]);
			out->minmaxs[3 + j] = LittleShort(in->maxs[j]);
		}

		p = LittleLong(in->contents);
		out->contents = p;

		out->cluster = LittleShort(in->cluster);
		out->area = LittleShort(in->area);

		// make unsigned long from signed short
		firstleafface = LittleShort(in->firstleafface) & 0xFFFF;
		out->nummarksurfaces = LittleShort(in->numleaffaces) & 0xFFFF;

		out->firstmarksurface = loadmodel->marksurfaces + firstleafface;
		if ((firstleafface + out->nummarksurfaces) > loadmodel->nummarksurfaces)
		{
			ri.Sys_Error(ERR_DROP, "%s: wrong marksurfaces position in %s",
				__func__, loadmodel->name);
		}
	}
}

static void
Mod_LoadMarksurfaces(gl3model_t *loadmodel, byte *mod_base, lump_t *l)
{
	int i, j, count;
	short *in;
	msurface_t **out;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		ri.Sys_Error(ERR_DROP, "%s: funny lump size in %s",
				__func__, loadmodel->name);
	}

	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->marksurfaces = out;
	loadmodel->nummarksurfaces = count;

	for (i = 0; i < count; i++)
	{
		j = LittleShort(in[i]);

		if ((j < 0) || (j >= loadmodel->numsurfaces))
		{
			ri.Sys_Error(ERR_DROP, "%s: bad surface number", __func__);
		}

		out[i] = loadmodel->surfaces + j;
	}
}

static void
Mod_LoadSurfedges(gl3model_t *loadmodel, byte *mod_base, lump_t *l)
{
	int i, count;
	int *in, *out;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		ri.Sys_Error(ERR_DROP, "%s: funny lump size in %s",
				__func__, loadmodel->name);
	}

	count = l->filelen / sizeof(*in);

	if ((count < 1) || (count >= MAX_MAP_SURFEDGES))
	{
		ri.Sys_Error(ERR_DROP, "%s: bad surfedges count in %s: %i",
				__func__, loadmodel->name, count);
	}

	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->surfedges = out;
	loadmodel->numsurfedges = count;

	for (i = 0; i < count; i++)
	{
		out[i] = LittleLong(in[i]);
	}
}

static void
Mod_LoadPlanes(gl3model_t *loadmodel, byte *mod_base, lump_t *l)
{
	int i, j;
	cplane_t *out;
	dplane_t *in;
	int count;
	int bits;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		ri.Sys_Error(ERR_DROP, "%s: funny lump size in %s",
				__func__, loadmodel->name);
	}

	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc(count * 2 * sizeof(*out));

	loadmodel->planes = out;
	loadmodel->numplanes = count;

	for (i = 0; i < count; i++, in++, out++)
	{
		bits = 0;

		for (j = 0; j < 3; j++)
		{
			out->normal[j] = LittleFloat(in->normal[j]);

			if (out->normal[j] < 0)
			{
				bits |= 1 << j;
			}
		}

		out->dist = LittleFloat(in->dist);
		out->type = LittleLong(in->type);
		out->signbits = bits;
	}
}

// calculate the size that Hunk_Alloc(), called by Mod_Load*() from Mod_LoadBrushModel(),
// will use (=> includes its padding), so we'll know how big the hunk needs to be
static int calcLumpHunkSize(const lump_t *l, int inSize, int outSize)
{
	if (l->filelen % inSize)
	{
		// Mod_Load*() will error out on this because of "funny size"
		// don't error out here because in Mod_Load*() it can print the functionname
		// (=> tells us what kind of lump) before shutting down the game
		return 0;
	}

	int count = l->filelen / inSize;
	int size = count * outSize;

	// round to cacheline, like Hunk_Alloc() does
	size = (size + 31) & ~31;
	return size;
}

#define QBSPHEADER               ('Q' | ('B' << 8) | ('S' << 16) | ('P' << 24))

static void
Mod_LoadBrushModel(gl3model_t *mod, void *buffer, int modfilelen)
{
	int i;
	dheader_t *header;
	byte *mod_base;

	if (mod != mod_known)
	{
		ri.Sys_Error(ERR_DROP, "Loaded a brush model after the world");
	}

	header = (dheader_t *)buffer;

	i = LittleLong(header->version);

	qboolean use_qbsp = false;
	int ident = LittleLong(header->ident);
	switch (ident) {
    case IDBSPHEADER:
        break;
    case QBSPHEADER:
        use_qbsp = true;
        ri.Sys_Error(ERR_DROP, "unsupported QBSP format: %s", mod->name);
        break;
    default:
        ri.Sys_Error(ERR_DROP, "unsupported unknown BSP format: %s", mod->name);
		break;
    }

	if (i != BSPVERSION)
	{
		ri.Sys_Error(ERR_DROP, "%s: %s has wrong version number (%i should be %i)",
				__func__, mod->name, i, BSPVERSION);
	}

	/* swap all the lumps */
	mod_base = (byte *)header;

	for (i = 0; i < sizeof(dheader_t) / 4; i++)
	{
		((int *)header)[i] = LittleLong(((int *)header)[i]);
	}

	// calculate the needed hunksize from the lumps
	int hunkSize = 0;
	hunkSize += calcLumpHunkSize(&header->lumps[LUMP_VERTEXES], sizeof(dvertex_t), sizeof(mvertex_t));
	hunkSize += calcLumpHunkSize(&header->lumps[LUMP_EDGES], sizeof(dedge_t), sizeof(medge_t));
	hunkSize += sizeof(medge_t) + 31; // for count+1 in Mod_LoadEdges()
	int surfEdgeCount = (header->lumps[LUMP_SURFEDGES].filelen+sizeof(int)-1)/sizeof(int);
	if(surfEdgeCount < MAX_MAP_SURFEDGES) // else it errors out later anyway
		hunkSize += calcLumpHunkSize(&header->lumps[LUMP_SURFEDGES], sizeof(int), sizeof(int));
	hunkSize += calcLumpHunkSize(&header->lumps[LUMP_LIGHTING], 1, 1);
	hunkSize += calcLumpHunkSize(&header->lumps[LUMP_PLANES], sizeof(dplane_t), sizeof(cplane_t)*2);
	hunkSize += calcTexinfoAndFacesSize(mod_base, &header->lumps[LUMP_FACES], &header->lumps[LUMP_TEXINFO]);
	hunkSize += calcLumpHunkSize(&header->lumps[LUMP_LEAFFACES], sizeof(short), sizeof(msurface_t *)); // yes, out is indeeed a pointer!
	hunkSize += calcLumpHunkSize(&header->lumps[LUMP_VISIBILITY], 1, 1);
	hunkSize += calcLumpHunkSize(&header->lumps[LUMP_LEAFS], sizeof(dleaf_t), sizeof(mleaf_t));
	hunkSize += calcLumpHunkSize(&header->lumps[LUMP_NODES], sizeof(dnode_t), sizeof(mnode_t));
	hunkSize += calcLumpHunkSize(&header->lumps[LUMP_MODELS], sizeof(dmodel_t), sizeof(mmodel_t));

	// gnemeth: load entities as well
	hunkSize += calcLumpHunkSize(&header->lumps[LUMP_ENTITIES], 1, 1);

	mod->extradata = Hunk_Begin(hunkSize);
	mod->type = mod_brush;

	/* load into heap */
	Mod_LoadVertexes(mod, mod_base, &header->lumps[LUMP_VERTEXES]);
	Mod_LoadEdges(mod, mod_base, &header->lumps[LUMP_EDGES]);
	Mod_LoadSurfedges(mod, mod_base, &header->lumps[LUMP_SURFEDGES]);
	Mod_LoadLighting(mod, mod_base, &header->lumps[LUMP_LIGHTING]);
	Mod_LoadPlanes(mod, mod_base, &header->lumps[LUMP_PLANES]);
	Mod_LoadTexinfo(mod, mod_base, &header->lumps[LUMP_TEXINFO]);
	Mod_LoadFaces(mod, mod_base, &header->lumps[LUMP_FACES]);
	Mod_LoadMarksurfaces(mod, mod_base, &header->lumps[LUMP_LEAFFACES]);
	Mod_LoadVisibility(mod, mod_base, &header->lumps[LUMP_VISIBILITY]);
	Mod_LoadLeafs(mod, mod_base, &header->lumps[LUMP_LEAFS]);
	Mod_LoadNodes(mod, mod_base, &header->lumps[LUMP_NODES]);
	Mod_LoadSubmodels (mod, mod_base, &header->lumps[LUMP_MODELS]);

	// gnemeth: load entities as well
	void* entities = mod_base + header->lumps[LUMP_ENTITIES].fileofs;
	mod->entities = (char*)Hunk_Alloc(header->lumps[LUMP_ENTITIES].filelen);
	memcpy(mod->entities, entities, header->lumps[LUMP_ENTITIES].filelen);

	mod->numframes = 2; /* regular and alternate animation */
}

static void
Mod_Free(gl3model_t *mod)
{
	Hunk_Free(mod->extradata);
	memset(mod, 0, sizeof(*mod));
}

void
GL3_Mod_FreeAll(void)
{
	int i;

	for (i = 0; i < mod_numknown; i++)
	{
		if (mod_known[i].extradatasize)
		{
			Mod_Free(&mod_known[i]);
		}
	}
}

extern void GL3_LoadMD2(gl3model_t *mod, void *buffer, int modfilelen);
extern void GL3_LoadSP2(gl3model_t *mod, void *buffer, int modfilelen);

/*
 * Loads in a model for the given name
 */
static gl3model_t *
Mod_ForName (char *name, gl3model_t *parent_model, qboolean crash)
{
	gl3model_t *mod;
	unsigned *buf;
	int i;

	if (!name[0])
	{
		ri.Sys_Error(ERR_DROP, "%s: NULL name", __func__);
	}

	/* inline models are grabbed only from worldmodel */
	if (name[0] == '*' && parent_model)
	{
		i = (int)strtol(name + 1, (char **)NULL, 10);

		if (i < 1 || i >= parent_model->numsubmodels)
		{
			ri.Sys_Error(ERR_DROP, "%s: bad inline model number",
					__func__);
		}

		return &parent_model->submodels[i];
	}

	/* search the currently loaded models */
	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		if (!mod->name[0])
		{
			continue;
		}

		if (!strcmp(mod->name, name))
		{
			return mod;
		}
	}

	/* find a free model slot spot */
	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		if (!mod->name[0])
		{
			break; /* free spot */
		}
	}

	if (i == mod_numknown)
	{
		if (mod_numknown == MAX_MOD_KNOWN)
		{
			ri.Sys_Error(ERR_DROP, "mod_numknown == MAX_MOD_KNOWN");
		}

		mod_numknown++;
	}

	strcpy(mod->name, name);

	/* load the file */
	int modfilelen = ri.FS_LoadFile(mod->name, (void **)&buf);

	if (!buf)
	{
		if (crash)
		{
			ri.Sys_Error(ERR_DROP, "%s: %s not found",
					__func__, mod->name);
		}

		memset(mod->name, 0, sizeof(mod->name));
		return NULL;
	}

	/* call the apropriate loader */
	switch (LittleLong(*(unsigned *)buf))
	{
		case IDALIASHEADER:
			GL3_LoadMD2(mod, buf, modfilelen);
			break;

		case IDSPRITEHEADER:
			GL3_LoadSP2(mod, buf, modfilelen);
			break;

		case IDBSPHEADER:
			Mod_LoadBrushModel(mod, buf, modfilelen);
			break;

		default:
			ri.Sys_Error(ERR_DROP, "%s: unknown fileid for %s",
					__func__, mod->name);
			break;
	}

	mod->extradatasize = Hunk_End();

	ri.FS_FreeFile(buf);

	return mod;
}


enum gl3_entity { entity_invalid, entity_worldspawn, entity_light };

static qboolean worldsun;
static vec3_t worldsunangle;
static qboolean shadowlight;
static vec3_t shadowlightorigin;
static vec3_t shadowlightangle;
static float shadowlightconeangle;
static float shadowlightradius;
static qboolean shadowlightspot;
static float shadowlightresolution;
static float shadowlightdarken;

static void GL3_HandleEntityKey(enum gl3_entity t, const char* key, size_t keylen, const char* value, size_t valuelen)
{
	if (keylen == 0) { return; }

	char* v = malloc(valuelen + 1);
	v[valuelen] = '\0';
	memcpy(v, value, valuelen);

	if (t == entity_worldspawn) {
		if (!strncmp(key, "_shadowsun", keylen)) {
			worldsun = true;
		}
		else if (!strncmp(key, "_shadowsunangle", keylen)) {
			// Cmd_TokenizeString (v);
			// worldsunangle[0] = Q_atof (Cmd_Argv(0));
			// worldsunangle[1] = Q_atof (Cmd_Argv(1));
			// worldsunangle[2] = Q_atof (Cmd_Argv(2));
		}
		else if (!strncmp(key, "fog", keylen)) {
			float d, r, g, b;
			sscanf(v, "%f %f %f %f", &d, &r, &g, &b);
			GL3_Fog_Set(r, g, b, d);
		}
	}
	else if (t == entity_light) {
		if (!strncmp(key, "_shadowlight", keylen)) {
			shadowlight = true;
		}
		else if (!strncmp(key, "origin", keylen)) {
			sscanf(v, "%f %f %f", &shadowlightorigin[0], &shadowlightorigin[1], &shadowlightorigin[2]);
		}
		else if (!strncmp(key, "mangle", keylen)) {
			sscanf(v, "%f %f %f", &shadowlightangle[0], &shadowlightangle[1], &shadowlightangle[2]);
			shadowlightspot = true;
		}
		else if (!strncmp(key, "angle", keylen)) {
			shadowlightconeangle = atof(v);
			shadowlightspot = true;
		}
		else if (!strncmp(key, "_shadowlightconeangle", keylen)) {
			shadowlightconeangle = atof(v);
			shadowlightspot = true;
		}
		else if (!strncmp(key, "_shadowlightradius", keylen)) {
			shadowlightradius = atof(v);
		}
		else if (!strncmp(key, "_shadowlightresolution", keylen)) {
			shadowlightresolution = atof(v);
		}
		else if (!strncmp(key, "_shadowlightdarken", keylen)) {
			shadowlightdarken = atof(v);
		}
	}
	free(v);
}

static void GL3_EndEntity(enum gl3_entity t)
{
	if (t == entity_light && shadowlight) {
		if (shadowlightspot) {
			GL3_Shadow_AddSpotLight(shadowlightorigin, shadowlightangle, shadowlightconeangle, shadowlightradius);
		}
		else {
			// R_Shadow_AddPointLight (shadowlightorigin, shadowlightradius);
		}
	}
	else if (t == entity_worldspawn && worldsun) {
		// R_Shadow_SetupSun (worldsunangle);
	}

	memset(shadowlightorigin, 0, sizeof(shadowlightorigin));
	memset(shadowlightangle, 0, sizeof(shadowlightangle));
	shadowlightconeangle = 0.0f;
	shadowlightradius = 0.0f;
	shadowlightresolution = 0.0f;
	shadowlightdarken = 0.0f;
	shadowlight = false;
	worldsun = false;
}

// gnemeth: parse the map entities to enable renderer featuers (fog, shadow lights, etc...)
static void GL3_ParseEntities (const char* ent_text)
{
	enum {
		parse_initial,
		parse_entity1,
		parse_entity2,
		parse_field_key,
		parse_field_value,
		parse_brushes,
		parse_comment,
	} state = parse_initial;

	size_t field_begin = 0;
	size_t field_end = 0;
	const char* field_key;
	const char* field_value;
	size_t field_key_len;
	size_t field_value_len;
	size_t textsize = strlen(ent_text);
	enum gl3_entity current_entity = entity_worldspawn;
	
	for (size_t offs = 0; offs < textsize; offs++) {
		char c = ent_text[offs];
		char cn = (offs < textsize-1) ? ent_text[offs+1] : 0;

		switch (state) {
		case parse_initial: {
			if (c == '/' && cn == '/') {
				state = parse_comment;
				offs++;
			}
			else if (c == '{') {
				state = parse_entity1;
			}
			break;
		}
		case parse_entity1: {
			if (c == '"') {
				state = parse_field_key;
				field_begin = offs + 1;
			}
			else if (c == '{') {
				state = parse_brushes;
				field_begin = offs + 1;
			}
			else if (c == '}') {
				state = parse_initial;
				GL3_EndEntity(current_entity);
				current_entity = entity_invalid;
			}
			break;
		}
		case parse_entity2: {
			if (c == '"') {
				state = parse_field_value;
				field_begin = offs + 1;
			}
			break;
		}
		case parse_field_key: {
			if (c == '"') {
				state = parse_entity2;
				field_key = ent_text+field_begin;
				field_key_len = offs-field_begin;
			}
			break;
		}
		case parse_field_value: {
			if (c == '"') {
				state = parse_entity1;
				field_value = ent_text+field_begin;
				field_value_len = offs-field_begin;

				if (!strncmp(field_key, "classname", field_key_len)) {
					if (!strncmp(field_value, "worldspawn", field_value_len)) {
						current_entity = entity_worldspawn;
					}
				}

				if (current_entity == entity_invalid && !strncmp(field_key, "_shadowlight", strlen("_shadowlight"))) {
					current_entity = entity_light;
				}

				GL3_HandleEntityKey (current_entity, field_key, field_key_len, field_value, field_value_len);
			}
			break;
		}
		case parse_brushes: {
			if (c == '}') {
				state = parse_entity1;
			}
			break;
		}
		case parse_comment:
			if (c == '\n') {
				state = parse_initial;
			}
			break;
		}
	}
}

/*
 * Specifies the model that will be used as the world
 */
void
GL3_BeginRegistration(char *model)
{
	char fullname[MAX_QPATH];
	cvar_t *flushmap;

	registration_sequence++;
	gl3_oldviewcluster = -1; /* force markleafs */

	gl3state.currentlightmap = -1;

	Com_sprintf(fullname, sizeof(fullname), "maps/%s.bsp", model);

	/* explicitly free the old map if different
	   this guarantees that mod_known[0] is the
	   world map */
	flushmap = ri.Cvar_Get("flushmap", "0", 0);

	if (strcmp(mod_known[0].name, fullname) || flushmap->value)
	{
		Mod_Free(&mod_known[0]);
	}

	gl3_worldmodel = Mod_ForName(fullname, NULL, true);

	gl3_viewcluster = -1;

	GL3_Shadow_Shutdown();
	GL3_Fog_Set(0.3f, 0.3f, 0.3f, 0.0f);
	GL3_ParseEntities(gl3_worldmodel->entities);
}

struct model_s *
GL3_RegisterModel(char *name)
{
	gl3model_t *mod;
	int i;
	dsprite_t *sprout;
	dmdl_t *pheader;

	mod = Mod_ForName(name, gl3_worldmodel, false);

	if (mod)
	{
		mod->registration_sequence = registration_sequence;

		/* register any images used by the models */
		if (mod->type == mod_sprite)
		{
			sprout = (dsprite_t *)mod->extradata;

			for (i = 0; i < sprout->numframes; i++)
			{
				mod->skins[i] = GL3_FindImage(sprout->frames[i].name, it_sprite);
			}
		}
		else if (mod->type == mod_alias)
		{
			pheader = (dmdl_t *)mod->extradata;

			for (i = 0; i < pheader->num_skins; i++)
			{
				mod->skins[i] = GL3_FindImage((char *)pheader + pheader->ofs_skins + i * MAX_SKINNAME, it_skin);
			}

			mod->numframes = pheader->num_frames;
		}
		else if (mod->type == mod_brush)
		{
			for (i = 0; i < mod->numtexinfo; i++)
			{
				mod->texinfo[i].image->registration_sequence = registration_sequence;
			}
		}
	}

	return mod;
}

void
GL3_EndRegistration(void)
{
	int i;
	gl3model_t *mod;

	if (Mod_HasFreeSpace() && GL3_ImageHasFreeSpace())
	{
		// should be enough space for load next maps
		return;
	}

	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		if (!mod->name[0])
		{
			continue;
		}

		if (mod->registration_sequence != registration_sequence)
		{
			/* don't need this model */
			Mod_Free(mod);
		}
	}

	GL3_FreeUnusedImages();
}
