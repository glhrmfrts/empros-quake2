#include "header/local.h"

#define MAX_BATCH_INDICES 4096

static unsigned int 	batch_indices[MAX_BATCH_INDICES];
static size_t 		batch_numindices;
static int 		batch_lmtexnum;

static size_t NumberOfIndicesForSurface(msurface_t* fa)
{
	if (fa->flags & SURF_DRAWTURB)
	{
		size_t count = 0;
		for (glpoly_t* bp = fa->polys; bp != NULL; bp = bp->next)
		{
			count += bp->numverts * 3 - 6;
		}
		return count;
	}
	else
	{
		return 3 * (fa->numedges - 2);
	}
}

void GL3_SurfBatch_Clear()
{
	batch_lmtexnum = -1;
	batch_numindices = 0;
}

void GL3_SurfBatch_Begin()
{
	GL3_BindVAO(gl3state.vaoWorld);
	GL3_BindEBO(0);
	GL3_SurfBatch_Clear();
}

void GL3_SurfBatch_Flush()
{
	if (batch_numindices == 0) { return; }

	glDrawElements(GL_TRIANGLES, batch_numindices, GL_UNSIGNED_INT, batch_indices);

	batch_numindices = 0;
}

void GL3_SurfBatch_Add(msurface_t* fa)
{
	size_t numindices = NumberOfIndicesForSurface(fa);
	if (batch_numindices + numindices > MAX_BATCH_INDICES)
	{
		GL3_SurfBatch_Flush();
	}

	unsigned int* dest = batch_indices + batch_numindices;
	if (fa->flags & SURF_DRAWTURB)
	{
		for (glpoly_t* bp = fa->polys; bp != NULL; bp = bp->next)
		{
			for (int i=2; i<bp->numverts; i++)
			{
				*dest++ = bp->vbo_first_vert;
				*dest++ = bp->vbo_first_vert + i - 1;
				*dest++ = bp->vbo_first_vert + i;
			}
		}
	}
	else
	{
		for (int i=2; i<fa->numedges; i++)
		{
			*dest++ = fa->polys->vbo_first_vert;
			*dest++ = fa->polys->vbo_first_vert + i - 1;
			*dest++ = fa->polys->vbo_first_vert + i;
		}
	}

	batch_numindices += numindices;
}

void GL3_SurfBatch_DrawSingle(msurface_t* fa)
{
	GL3_SurfBatch_Add(fa);
	GL3_SurfBatch_Flush();
}

void GL3_SurfBatch_RenderWorldPoly(entity_t *currententity, gl3image_t* image, msurface_t *fa)
{
	c_brush_polys++;

	if ((gl3state.renderPass == RENDER_PASS_SCENE) && !(fa->flags & SURF_DRAWTURB) && fa->lightmaptexturenum != batch_lmtexnum)
	{
		GL3_SurfBatch_Flush();
		batch_lmtexnum = fa->lightmaptexturenum;
		GL3_BindLightmap(fa->lightmaptexturenum);
	}

	GL3_SurfBatch_Add(fa);
}
