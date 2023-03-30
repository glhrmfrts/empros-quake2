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
 * Surface generation and drawing
 *
 * =======================================================================
 */

#include <assert.h>

#include "header/local.h"

int c_visible_lightmaps;
int c_visible_textures;
msurface_t *gl3_alpha_surfaces;
msurface_t *g_ssao_surfaces;

gl3lightmapstate_t gl3_lms;

#define BACKFACE_EPSILON 0.01

extern gl3image_t gl3textures[MAX_GL3TEXTURES];
extern int numgl3textures;

static void Setup3DAttributes()
{
	glEnableVertexAttribArray(GL3_ATTRIB_POSITION);
	qglVertexAttribPointer(GL3_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, sizeof(gl3_3D_vtx_t), 0);

	glEnableVertexAttribArray(GL3_ATTRIB_TEXCOORD);
	qglVertexAttribPointer(GL3_ATTRIB_TEXCOORD, 2, GL_FLOAT, GL_FALSE, sizeof(gl3_3D_vtx_t), offsetof(gl3_3D_vtx_t, texCoord));

	glEnableVertexAttribArray(GL3_ATTRIB_LMTEXCOORD);
	qglVertexAttribPointer(GL3_ATTRIB_LMTEXCOORD, 2, GL_FLOAT, GL_FALSE, sizeof(gl3_3D_vtx_t), offsetof(gl3_3D_vtx_t, lmTexCoord));

	glEnableVertexAttribArray(GL3_ATTRIB_NORMAL);
	qglVertexAttribPointer(GL3_ATTRIB_NORMAL, 3, GL_FLOAT, GL_FALSE, sizeof(gl3_3D_vtx_t), offsetof(gl3_3D_vtx_t, normal));

	glEnableVertexAttribArray(GL3_ATTRIB_LIGHTFLAGS);
	qglVertexAttribIPointer(GL3_ATTRIB_LIGHTFLAGS, 1, GL_UNSIGNED_INT, sizeof(gl3_3D_vtx_t), offsetof(gl3_3D_vtx_t, lightFlags));

	for (int map = 0; map < MAX_LIGHTMAPS_PER_SURFACE; map++) {
		size_t offs = offsetof(gl3_3D_vtx_t, styles);
		offs += sizeof(GLuint) * map;
		glEnableVertexAttribArray(GL3_ATTRIB_STYLE0 + map);
		qglVertexAttribIPointer(GL3_ATTRIB_STYLE0 + map, 1, GL_UNSIGNED_INT, sizeof(gl3_3D_vtx_t), offs);
	}
}

void GL3_SurfInit(void)
{
	// init the VAO and VBO for the standard vertexdata: 10 floats and 1 uint
	// (X, Y, Z), (S, T), (LMS, LMT), (normX, normY, normZ) ; lightFlags - last two groups for lightmap/dynlights

	glGenVertexArrays(1, &gl3state.vao3D);
	GL3_BindVAO(gl3state.vao3D);

	glGenBuffers(1, &gl3state.vbo3D);
	GL3_BindVBO(gl3state.vbo3D);

	if(gl3config.useBigVBO)
	{
	 	gl3state.vbo3Dsize = 5*1024*1024; // a 5MB buffer seems to work well?
	 	gl3state.vbo3DcurOffset = 0;
	 	glBufferData(GL_ARRAY_BUFFER, gl3state.vbo3Dsize, NULL, GL_STREAM_DRAW); // allocate/reserve that data
	}

	Setup3DAttributes();


	glGenVertexArrays(1, &gl3state.vaoWorld);
	GL3_BindVAO(gl3state.vaoWorld);

	glGenBuffers(1, &gl3state.vboWorld);
	GL3_BindVBO(gl3state.vboWorld);

	Setup3DAttributes();


	// init VAO and VBO for model vertexdata: 9 floats
	// (X,Y,Z), (S,T), (R,G,B,A)

	glGenVertexArrays(1, &gl3state.vaoAlias);
	GL3_BindVAO(gl3state.vaoAlias);

	glGenBuffers(1, &gl3state.vboAlias);
	GL3_BindVBO(gl3state.vboAlias);

	glEnableVertexAttribArray(GL3_ATTRIB_POSITION);
	qglVertexAttribPointer(GL3_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, sizeof(gl3_alias_vtx_t), 0);

	glEnableVertexAttribArray(GL3_ATTRIB_TEXCOORD);
	qglVertexAttribPointer(GL3_ATTRIB_TEXCOORD, 2, GL_FLOAT, GL_FALSE, sizeof(gl3_alias_vtx_t), 3*sizeof(GLfloat));

	glEnableVertexAttribArray(GL3_ATTRIB_COLOR);
	qglVertexAttribPointer(GL3_ATTRIB_COLOR, 4, GL_FLOAT, GL_FALSE, sizeof(gl3_alias_vtx_t), 5*sizeof(GLfloat));

	glEnableVertexAttribArray(GL3_ATTRIB_NORMAL);
	qglVertexAttribPointer(GL3_ATTRIB_NORMAL, 3, GL_FLOAT, GL_FALSE, sizeof(gl3_alias_vtx_t), 9*sizeof(GLfloat));

	glGenBuffers(1, &gl3state.eboAlias);

	// init VAO and VBO for particle vertexdata: 9 floats
	// (X,Y,Z), (point_size,distace_to_camera), (R,G,B,A)

	glGenVertexArrays(1, &gl3state.vaoParticle);
	GL3_BindVAO(gl3state.vaoParticle);

	glGenBuffers(1, &gl3state.vboParticle);
	GL3_BindVBO(gl3state.vboParticle);

	glEnableVertexAttribArray(GL3_ATTRIB_POSITION);
	qglVertexAttribPointer(GL3_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, 9*sizeof(GLfloat), 0);

	// TODO: maybe move point size and camera origin to UBO and calculate distance in vertex shader
	glEnableVertexAttribArray(GL3_ATTRIB_TEXCOORD); // it's abused for (point_size, distance) here..
	qglVertexAttribPointer(GL3_ATTRIB_TEXCOORD, 2, GL_FLOAT, GL_FALSE, 9*sizeof(GLfloat), 3*sizeof(GLfloat));

	glEnableVertexAttribArray(GL3_ATTRIB_COLOR);
	qglVertexAttribPointer(GL3_ATTRIB_COLOR, 4, GL_FLOAT, GL_FALSE, 9*sizeof(GLfloat), 5*sizeof(GLfloat));
}

void GL3_SurfShutdown(void)
{
	glDeleteBuffers(1, &gl3state.vbo3D);
	gl3state.vbo3D = 0;
	glDeleteVertexArrays(1, &gl3state.vao3D);
	gl3state.vao3D = 0;

	glDeleteBuffers(1, &gl3state.vboWorld);
	gl3state.vboWorld = 0;
	glDeleteVertexArrays(1, &gl3state.vaoWorld);
	gl3state.vaoWorld = 0;

	glDeleteBuffers(1, &gl3state.eboAlias);
	gl3state.eboAlias = 0;
	glDeleteBuffers(1, &gl3state.vboAlias);
	gl3state.vboAlias = 0;
	glDeleteVertexArrays(1, &gl3state.vaoAlias);
	gl3state.vaoAlias = 0;
}

/*
 * Returns the proper texture for a given time and base texture
 */
static gl3image_t *
TextureAnimation(entity_t *currententity, mtexinfo_t *tex)
{
	int c;

	if (!tex->next)
	{
		return tex->image;
	}

	c = currententity->frame % tex->numframes;

	while (c)
	{
		tex = tex->next;
		c--;
	}

	return tex->image;
}

static void
DrawTriangleOutlines(void)
{
	STUB_ONCE("TODO: Implement for gl_showtris support!");
#if 0
	int i, j;
	glpoly_t *p;

	if (!gl_showtris->value)
	{
		return;
	}

	glDisable(GL_TEXTURE_2D);
	glDisable(GL_DEPTH_TEST);
	glColor4f(1, 1, 1, 1);

	for (i = 0; i < MAX_LIGHTMAPS; i++)
	{
		msurface_t *surf;

		for (surf = gl3_lms.lightmap_surfaces[i];
				surf != 0;
				surf = surf->lightmapchain)
		{
			p = surf->polys;

			for ( ; p; p = p->chain)
			{
				for (j = 2; j < p->numverts; j++)
				{
					GLfloat vtx[12];
					unsigned int k;

					for (k=0; k<3; k++)
					{
						vtx[0+k] = p->vertices [ 0 ][ k ];
						vtx[3+k] = p->vertices [ j - 1 ][ k ];
						vtx[6+k] = p->vertices [ j ][ k ];
						vtx[9+k] = p->vertices [ 0 ][ k ];
					}

					glEnableClientState( GL_VERTEX_ARRAY );

					glVertexPointer( 3, GL_FLOAT, 0, vtx );
					glDrawArrays( GL_LINE_STRIP, 0, 4 );

					glDisableClientState( GL_VERTEX_ARRAY );
				}
			}
		}
	}

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_TEXTURE_2D);
#endif // 0
}

/*
 * Draw water surfaces and windows.
 * The BSP tree is waled front to back, so unwinding the chain.
 * of alpha_surfaces will draw back to front, giving proper ordering.
 */
void
GL3_DrawAlphaSurfaces(void)
{
	msurface_t *s;

	/* go back to the world matrix */
	gl3state.uni3DData.transModelMat4 = gl3_identityMat4;
	GL3_UpdateUBO3D();

	glEnable(GL_BLEND);

	GL3_SurfBatch_Begin();

	for (s = gl3_alpha_surfaces; s != NULL; s = s->texturechain)
	{
		GL3_Bind(s->texinfo->image->texnum);

		c_brush_polys++;

		float alpha = 1.0f;
		if (s->texinfo->flags & SURF_TRANS33)
		{
			alpha = 0.333f;
		}
		else if (s->texinfo->flags & SURF_TRANS66)
		{
			alpha = 0.666f;
		}
		if(alpha != gl3state.uni3DData.alpha)
		{
			gl3state.uni3DData.alpha = alpha;
			GL3_UpdateUBO3D();
		}

		if (s->flags & SURF_DRAWTURB)
		{
			GL3_UseProgram(gl3state.si3Dturb.shaderProgram);
			GL3_SurfBatch_DrawSingle(s);
		}
		else if (s->flags & SURF_DRAWTURBLIT)
		{
			GL3_UseProgram(gl3state.si3DlmTurb.shaderProgram);
			GL3_BindLightmap(s->lightmaptexturenum);
			GL3_SurfBatch_DrawSingle(s);
		}
		else if (s->texinfo->flags & SURF_FLOWING)
		{
			GL3_UseProgram(gl3state.si3DtransFlow.shaderProgram);
			GL3_SurfBatch_DrawSingle(s);
		}
		else
		{
			GL3_UseProgram(gl3state.si3Dtrans.shaderProgram);
			GL3_SurfBatch_DrawSingle(s);
		}
	}

	gl3state.uni3DData.alpha = 1.0f;
	GL3_UpdateUBO3D();

	glDisable(GL_BLEND);

	gl3_alpha_surfaces = NULL;
}

void
GL3_DrawTextureChains(entity_t *currententity)
{
	int i;
	msurface_t *s;
	gl3image_t *image;

	c_visible_textures = 0;

	GL3_SurfBatch_Begin();

	if (gl3state.renderPass == RENDER_PASS_SSAO)
	{
		for (msurface_t* surf = g_ssao_surfaces; surf; surf = surf->texturechain)
		{
			GL3_SurfBatch_Add(surf);
		}
		GL3_SurfBatch_Flush();
		g_ssao_surfaces = NULL;
		return;
	}

	for (i = 0, image = gl3textures; i < numgl3textures; i++, image++)
	{
		if (!image->registration_sequence)
		{
			continue;
		}

		s = image->texturechain;

		if (!s)
		{
			continue;
		}

		c_visible_textures++;

		GL3_Bind(image->texnum);

		GL3_SurfBatch_Clear();

		int is_emissive = (s->texinfo->flags & SURF_LIGHT);

		if (gl3state.renderPass == RENDER_PASS_SCENE)
		{
			if (s->flags & SURF_DRAWTURB)
			{
				GL3_UseProgram(gl3state.si3Dturb.shaderProgram);
			}
			else if (s->flags & SURF_DRAWTURBLIT)
			{
				GL3_UseProgram(gl3state.si3DlmTurb.shaderProgram);
			}
			else if (s->texinfo->flags & SURF_FLOWING)
			{
				GL3_UseProgram(gl3state.si3DlmFlow.shaderProgram);
			}
			else
			{
				gl3state.uni3DData.emission = is_emissive ? 1.0f : 0.0f;
				GL3_UpdateUBO3D();
				GL3_UseProgram(gl3state.si3Dlm.shaderProgram);
			}
		}

		for ( ; s; s = s->texturechain)
		{
			if ((s->texinfo->flags & SURF_LIGHT) != is_emissive)
			{
				is_emissive = (s->texinfo->flags & SURF_LIGHT);
				gl3state.uni3DData.emission = is_emissive ? 1.0f : 0.0f;
				GL3_UpdateUBO3D();
			}
			GL3_SurfBatch_RenderWorldPoly(currententity, image, s);
		}

		GL3_SurfBatch_Flush();

		image->texturechain = NULL;
	}

	gl3state.uni3DData.emission = 0.0f;
	// TODO: maybe one loop for normal faces and one for SURF_DRAWTURB ???
}

void
GL3_DrawTextureChainsShadowPass(entity_t *currententity)
{
	int i;
	msurface_t *s;
	gl3image_t *image;

	c_visible_textures = 0;

	GL3_SurfBatch_Begin();

	for (i = 0, image = gl3textures; i < numgl3textures; i++, image++)
	{
		if (!image->registration_sequence) continue;

		s = image->texturechain;
		if (!s) continue;

		c_visible_textures++;

		for ( ; s; s = s->texturechain)
		{
			GL3_SurfBatch_RenderWorldPoly(currententity, image, s);
		}
		image->texturechain = NULL;
	}

	GL3_SurfBatch_Flush();
}

static void
DrawInlineBModel(entity_t *currententity, gl3model_t *currentmodel, const vec3_t modelorg)
{
	int i, k;
	cplane_t *pplane;
	float dot;
	msurface_t *psurf;
	dlight_t *lt;

	/* calculate dynamic lighting for bmodel */
	lt = gl3_newrefdef.dlights;

	if (gl3state.renderPass == RENDER_PASS_SCENE)
	{
		for (k = 0; k < gl3_newrefdef.num_dlights; k++, lt++)
		{
			GL3_MarkLights(lt, 1 << k, currentmodel->nodes + currentmodel->firstnode);
		}
	}

	psurf = &currentmodel->surfaces[currentmodel->firstmodelsurface];

	if (currententity->flags & RF_TRANSLUCENT)
	{
		glEnable(GL_BLEND);
	}

	GL3_SurfBatch_Begin();

	/* draw texture */
	for (i = 0; i < currentmodel->nummodelsurfaces; i++, psurf++)
	{
		/* find which side of the node we are on */
		pplane = psurf->plane;

		dot = DotProduct(modelorg, pplane->normal) - pplane->dist;

		/* draw the polygon */
		if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			gl3image_t *image = TextureAnimation(currententity, psurf->texinfo);

			if (gl3state.renderPass == RENDER_PASS_SCENE)
			{
				if (psurf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66))
				{
					/* add to the translucent chain */
					psurf->texturechain = gl3_alpha_surfaces;
					gl3_alpha_surfaces = psurf;
				}
				else if (!(psurf->flags & SURF_DRAWTURB))
				{
					GL3_Bind(image->texnum);
					GL3_BindLightmap(psurf->lightmaptexturenum);
					if (psurf->texinfo->flags & SURF_FLOWING)
					{
						GL3_UseProgram(gl3state.si3DlmFlow.shaderProgram);
					}
					else
					{
						GL3_UseProgram(gl3state.si3Dlm.shaderProgram);
					}
					GL3_SurfBatch_DrawSingle(psurf);
				}
				else
				{
					GL3_Bind(image->texnum);
					GL3_UseProgram(gl3state.si3Dturb.shaderProgram);
					GL3_SurfBatch_DrawSingle(psurf);
				}
			}
			else
			{
				// shadow map or ssao
				GL3_SurfBatch_Add(psurf);
			}
		}
	}

	if (gl3state.renderPass != RENDER_PASS_SCENE)
	{
		GL3_SurfBatch_Flush();
	}

	if (currententity->flags & RF_TRANSLUCENT)
	{
		glDisable(GL_BLEND);
	}
}

void
GL3_DrawBrushModel(entity_t *e, gl3model_t *currentmodel)
{
	vec3_t mins, maxs;
	int i;
	qboolean rotated;

	if (currentmodel->nummodelsurfaces == 0)
	{
		return;
	}

	gl3state.currenttexture = -1;

	if (e->angles[0] || e->angles[1] || e->angles[2])
	{
		rotated = true;
		for (i = 0; i < 3; i++)
		{
			mins[i] = e->origin[i] - currentmodel->radius;
			maxs[i] = e->origin[i] + currentmodel->radius;
		}
	}
	else
	{
		rotated = false;
		VectorAdd(e->origin, currentmodel->mins, mins);
		VectorAdd(e->origin, currentmodel->maxs, maxs);
	}

	if (GL3_CullBox(mins, maxs))
	{
		return;
	}

	if (gl_zfix->value)
	{
		glEnable(GL_POLYGON_OFFSET_FILL);
	}

	vec3_t modelorg;
	if (gl3state.renderPass == RENDER_PASS_SHADOW && gl3state.currentShadowLight)
	{
		VectorSubtract(gl3state.currentShadowLight->position, e->origin, modelorg);
	}
	else
	{
		VectorSubtract(gl3_newrefdef.vieworg, e->origin, modelorg);
	}

	if (rotated)
	{
		vec3_t temp;
		vec3_t forward, right, up;

		VectorCopy(modelorg, temp);
		AngleVectors(e->angles, forward, right, up);
		modelorg[0] = DotProduct(temp, forward);
		modelorg[1] = -DotProduct(temp, right);
		modelorg[2] = DotProduct(temp, up);
	}

	//glPushMatrix();
	hmm_mat4 oldMat = gl3state.uni3DData.transModelMat4;

	e->angles[0] = -e->angles[0];
	e->angles[2] = -e->angles[2];
	GL3_RotateForEntity(e);
	e->angles[0] = -e->angles[0];
	e->angles[2] = -e->angles[2];

	DrawInlineBModel(e, currentmodel, modelorg);

	// glPopMatrix();
	gl3state.uni3DData.transModelMat4 = oldMat;
	GL3_UpdateUBO3D();

	if (gl_zfix->value)
	{
		glDisable(GL_POLYGON_OFFSET_FILL);
	}
}

void
GL3_RecursiveWorldNode(entity_t* currententity, mnode_t* node, const vec3_t modelorg)
{
	int c, side, sidebit;
	cplane_t *plane;
	msurface_t *surf, **mark;
	mleaf_t *pleaf;
	float dot;
	gl3image_t *image;

	if (node->contents == CONTENTS_SOLID)
	{
		return; /* solid */
	}

	if (node->visframe != gl3_visframecount)
	{
		return;
	}

	if (GL3_CullBox(node->minmaxs, node->minmaxs + 3))
	{
		return;
	}

	/* if a leaf node, draw stuff */
	if (node->contents != -1)
	{
		pleaf = (mleaf_t *)node;

		/* check for door connected areas */
		if (gl3_newrefdef.areabits)
		{
			if (!(gl3_newrefdef.areabits[pleaf->area >> 3] & (1 << (pleaf->area & 7))))
			{
				return; /* not visible */
			}
		}

		mark = pleaf->firstmarksurface;
		c = pleaf->nummarksurfaces;

		if (c)
		{
			do
			{
				(*mark)->visframe = gl3_framecount;
				mark++;
			}
			while (--c);
		}

		return;
	}

	/* node is just a decision point, so go down the apropriate
	   sides find which side of the node we are on */
	plane = node->plane;

	switch (plane->type)
	{
		case PLANE_X:
			dot = modelorg[0] - plane->dist;
			break;
		case PLANE_Y:
			dot = modelorg[1] - plane->dist;
			break;
		case PLANE_Z:
			dot = modelorg[2] - plane->dist;
			break;
		default:
			dot = DotProduct(modelorg, plane->normal) - plane->dist;
			break;
	}

	if (dot >= 0)
	{
		side = 0;
		sidebit = 0;
	}
	else
	{
		side = 1;
		sidebit = SURF_PLANEBACK;
	}

	/* recurse down the children, front side first */
	GL3_RecursiveWorldNode(currententity, node->children[side], modelorg);

	/* draw stuff */
	for (c = node->numsurfaces,
		 surf = gl3_worldmodel->surfaces + node->firstsurface;
		 c; c--, surf++)
	{
		if (surf->visframe != gl3_framecount)
		{
			continue;
		}

		if ((surf->flags & SURF_PLANEBACK) != sidebit)
		{
			continue; /* wrong side */
		}

		if (gl3state.renderPass == RENDER_PASS_SSAO)
		{
			surf->texturechain = g_ssao_surfaces;
			g_ssao_surfaces = surf;
		}
		else
		{
			surf->texturechain = NULL;
			if (surf->texinfo->flags & SURF_SKY)
			{
				/* just adds to visible sky bounds */
				GL3_AddSkySurface(surf);
			}
			else if (surf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66))
			{
				/* add to the translucent chain */
				surf->texturechain = gl3_alpha_surfaces;
				gl3_alpha_surfaces = surf;
				gl3_alpha_surfaces->texinfo->image = TextureAnimation(currententity, surf->texinfo);
			}
			else
			{
				/* the polygon is visible, so add it to the texture sorted chain */
				image = TextureAnimation(currententity, surf->texinfo);
				surf->texturechain = image->texturechain;
				image->texturechain = surf;
			}
		}
	}

	/* recurse down the back side */
	GL3_RecursiveWorldNode(currententity, node->children[!side], modelorg);
}

void
GL3_DrawWorld(void)
{
	entity_t ent;

	if (!r_drawworld->value)
	{
		return;
	}

	if (gl3_newrefdef.rdflags & RDF_NOWORLDMODEL)
	{
		return;
	}

	gl3_alpha_surfaces = NULL;

	vec3_t modelorg;
	VectorCopy(gl3_newrefdef.vieworg, modelorg);

	/* auto cycle the world frame for texture animation */
	memset(&ent, 0, sizeof(ent));
	ent.frame = (int)(gl3_newrefdef.time * 2);

	GL3_ClearSkyBox();
	GL3_RecursiveWorldNode(&ent, gl3_worldmodel->nodes, modelorg);
	GL3_DrawTextureChains(&ent);
	GL3_DrawSkyBox();
	DrawTriangleOutlines();
}

