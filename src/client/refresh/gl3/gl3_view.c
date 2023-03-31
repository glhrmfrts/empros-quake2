#include "header/local.h"

static int
SignbitsForPlane(cplane_t *out)
{
	int bits, j;

	/* for fast box on planeside test */
	bits = 0;

	for (j = 0; j < 3; j++)
	{
		if (out->normal[j] < 0)
		{
			bits |= 1 << j;
		}
	}

	return bits;
}

void GL3_SetViewParams(const vec3_t pos, const vec3_t angles, float fovX, float fovY, float nearZ, float farZ, float aspectRatio)
{
	gl3state.viewParams.fovX = fovX;
	gl3state.viewParams.fovY = fovY;
	VectorCopy(pos, gl3state.viewParams.origin);
	VectorCopy(angles, gl3state.viewParams.angles);
	AngleVectors(
		angles,
		gl3state.viewParams.vforward,
		gl3state.viewParams.vright,
		gl3state.viewParams.vup
	);

	/* rotate VFORWARD right by FOV_X/2 degrees */
	RotatePointAroundVector(gl3state.viewParams.frustum[0].normal,
				gl3state.viewParams.vup,
				gl3state.viewParams.vforward,
				-(90 - fovX / 2));
	/* rotate VFORWARD left by FOV_X/2 degrees */
	RotatePointAroundVector(gl3state.viewParams.frustum[1].normal,
				gl3state.viewParams.vup,
				gl3state.viewParams.vforward,
				90 - fovX / 2);
	/* rotate VFORWARD up by FOV_Y/2 degrees */
	RotatePointAroundVector(gl3state.viewParams.frustum[2].normal,
				gl3state.viewParams.vright,
				gl3state.viewParams.vforward,
				90 - fovY / 2);
	/* rotate VFORWARD down by FOV_Y/2 degrees */
	RotatePointAroundVector(gl3state.viewParams.frustum[3].normal,
				gl3state.viewParams.vright,
				gl3state.viewParams.vforward,
				-(90 - fovY / 2));

	for (int i = 0; i < 4; i++)
	{
		gl3state.viewParams.frustum[i].type = PLANE_ANYZ;
		gl3state.viewParams.frustum[i].dist = DotProduct(gl3state.viewParams.origin, gl3state.viewParams.frustum[i].normal);
		gl3state.viewParams.frustum[i].signbits = SignbitsForPlane(&gl3state.viewParams.frustum[i]);
	}

	// Define vertices
	{
		// first put Z axis going up
		hmm_mat4 viewMat = gl3_identityMat4;

		// now rotate by view angles
		hmm_mat4 rotMat = rotAroundAxisZYX(angles[1], angles[0] + 90, angles[2]);
		viewMat = HMM_MultiplyMat4( viewMat, rotMat );

		rotMat = rotAroundAxisZYX(90, 0, 0);
		viewMat = HMM_MultiplyMat4( viewMat, rotMat );

		// TODO: zoom?

		nearZ = max(nearZ, 0.0f);
		farZ = max(farZ, nearZ);

		float halfViewSize = tanf((fovY) * M_PI / 360.0);

		hmm_vec4 cnear, cfar;
		cnear.Z = nearZ;
		cnear.Y = cnear.Z * halfViewSize;
		cnear.X = cnear.Y * aspectRatio;
		cfar.Z = farZ;
		cfar.Y = cfar.Z * halfViewSize;
		cfar.X = cfar.Y * aspectRatio;

		hmm_vec4* vertices = gl3state.viewParams.vertices;
		vertices[0] = HMM_MultiplyMat4ByVec4(viewMat, cnear);
		vertices[1] = HMM_MultiplyMat4ByVec4(viewMat, (hmm_vec4){cnear.X, -cnear.Y, cnear.Z, 1.0f});
		vertices[2] = HMM_MultiplyMat4ByVec4(viewMat, (hmm_vec4){-cnear.X, -cnear.Y, cnear.Z, 1.0f});
		vertices[3] = HMM_MultiplyMat4ByVec4(viewMat, (hmm_vec4){-cnear.X, cnear.Y, cnear.Z, 1.0f});
		vertices[4] = HMM_MultiplyMat4ByVec4(viewMat, cfar);
		vertices[5] = HMM_MultiplyMat4ByVec4(viewMat, (hmm_vec4){cfar.X, -cfar.Y, cfar.Z, 1.0f});
		vertices[6] = HMM_MultiplyMat4ByVec4(viewMat, (hmm_vec4){-cfar.X, -cfar.Y, cfar.Z, 1.0f});
		vertices[7] = HMM_MultiplyMat4ByVec4(viewMat, (hmm_vec4){-cfar.X, cfar.Y, cfar.Z, 1.0f});

		if (r_shadowmap->value) for (int i = 0; i < 8; i++)
		{
			vertices[i].X += pos[0];
			vertices[i].Y += pos[1];
			vertices[i].Z += pos[2];
		}
	}
}

void GL3_SetupViewCluster()
{
	mleaf_t *leaf;

	/* current viewcluster */
	if (!(gl3_newrefdef.rdflags & RDF_NOWORLDMODEL))
	{
		gl3_oldviewcluster = gl3_viewcluster;
		gl3_oldviewcluster2 = gl3_viewcluster2;
		leaf = GL3_Mod_PointInLeaf(gl3state.viewParams.origin, gl3_worldmodel);
		gl3_viewcluster = gl3_viewcluster2 = leaf->cluster;

		//R_Printf(PRINT_ALL, "player cluster = %d\n", leaf->cluster);

		/* check above and below so crossing solid water doesn't draw wrong */
		if (!leaf->contents)
		{
			/* look down a bit */
			vec3_t temp;

			VectorCopy(gl3state.viewParams.origin, temp);
			temp[2] -= 16;
			leaf = GL3_Mod_PointInLeaf(temp, gl3_worldmodel);

			if (!(leaf->contents & CONTENTS_SOLID) &&
				(leaf->cluster != gl3_viewcluster2))
			{
				gl3_viewcluster2 = leaf->cluster;
			}
		}
		else
		{
			/* look up a bit */
			vec3_t temp;

			VectorCopy(gl3state.viewParams.origin, temp);
			temp[2] += 16;
			leaf = GL3_Mod_PointInLeaf(temp, gl3_worldmodel);

			if (!(leaf->contents & CONTENTS_SOLID) &&
				(leaf->cluster != gl3_viewcluster2))
			{
				gl3_viewcluster2 = leaf->cluster;
			}
		}
	}
}

/*
 * Mark the leaves and nodes that are
 * in the PVS for the current cluster
 */
void GL3_MarkLeaves(void)
{
	const byte *vis;
	YQ2_ALIGNAS_TYPE(int) byte fatvis[MAX_MAP_LEAFS / 8];
	mnode_t *node;
	int i, c;
	mleaf_t *leaf;
	int cluster;

	if ((gl3_oldviewcluster == gl3_viewcluster) &&
		(gl3_oldviewcluster2 == gl3_viewcluster2) &&
		!r_novis->value &&
		(gl3_viewcluster != -1))
	{
		return;
	}

	/* development aid to let you run around
	   and see exactly where the pvs ends */
	if (r_lockpvs->value)
	{
		return;
	}

	gl3_visframecount++;
	gl3_oldviewcluster = gl3_viewcluster;
	gl3_oldviewcluster2 = gl3_viewcluster2;

	if (r_novis->value || (gl3_viewcluster == -1) || !gl3_worldmodel->vis)
	{
		/* mark everything */
		for (i = 0; i < gl3_worldmodel->numleafs; i++)
		{
			gl3_worldmodel->leafs[i].visframe = gl3_visframecount;
		}

		for (i = 0; i < gl3_worldmodel->numnodes; i++)
		{
			gl3_worldmodel->nodes[i].visframe = gl3_visframecount;
		}

		return;
	}

	vis = GL3_Mod_ClusterPVS(gl3_viewcluster, gl3_worldmodel);

	/* may have to combine two clusters because of solid water boundaries */
	if (gl3_viewcluster2 != gl3_viewcluster)
	{
		memcpy(fatvis, vis, (gl3_worldmodel->numleafs + 7) / 8);
		vis = GL3_Mod_ClusterPVS(gl3_viewcluster2, gl3_worldmodel);
		c = (gl3_worldmodel->numleafs + 31) / 32;

		for (i = 0; i < c; i++)
		{
			((int *)fatvis)[i] |= ((int *)vis)[i];
		}

		vis = fatvis;
	}

	for (i = 0, leaf = gl3_worldmodel->leafs;
		 i < gl3_worldmodel->numleafs;
		 i++, leaf++)
	{
		cluster = leaf->cluster;

		if (cluster == -1)
		{
			continue;
		}

		if (vis[cluster >> 3] & (1 << (cluster & 7)))
		{
			node = (mnode_t *)leaf;

			do
			{
				if (node->visframe == gl3_visframecount)
				{
					break;
				}

				node->visframe = gl3_visframecount;
				node = node->parent;
			}
			while (node);
		}
	}
}

/*
 * Returns true if the box is completely outside the frustom
 */
qboolean GL3_CullBox(const vec3_t mins, const vec3_t maxs)
{
	int i;

	if (!gl_cull->value)
	{
		return false;
	}

	for (i = 0; i < 4; i++)
	{
		if (BOX_ON_PLANE_SIDE(mins, maxs, &gl3state.viewParams.frustum[i]) == 2)
		{
			return true;
		}
	}

	return false;
}

qboolean GL3_CullSphere(const vec3_t pos, float radius)
{
	if (!gl_cull->value) return false;

	float radiusSqr = radius*radius*1.5f;
	vec3_t sphereToView;
	VectorSubtract(pos, gl3state.viewParams.origin, sphereToView);
	if (DotProduct(sphereToView, sphereToView) <= radiusSqr)
		return false;

	for (int i = 0; i < 4; i++)
	{
		cplane_t* plane = &gl3state.viewParams.frustum[i];
		float dist = DotProduct(pos, plane->normal) - plane->dist;
		if (dist > radius)
		{
			return false;
		}
	}
	return true;
}

// equivalent to R_MYgluPerspective() but returning a matrix instead of setting internal OpenGL state
hmm_mat4 GL3_MYgluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zoom, GLdouble zNear, GLdouble zFar)
{
	// calculation of left, right, bottom, top is from R_MYgluPerspective() of old gl backend
	// which seems to be slightly different from the real gluPerspective()
	// and thus also from HMM_Perspective()
	GLdouble left, right, bottom, top;
	float A, B, C, D;

	top = zNear * tan(fovy * M_PI / 360.0) * zoom;
	bottom = -top;

	left = bottom * aspect;
	right = top * aspect;

	// TODO:  stereo stuff
	// left += - gl1_stereo_convergence->value * (2 * gl_state.camera_separation) / zNear;
	// right += - gl1_stereo_convergence->value * (2 * gl_state.camera_separation) / zNear;

	// the following emulates glFrustum(left, right, bottom, top, zNear, zFar)
	// see https://www.khronos.org/registry/OpenGL-Refpages/gl2.1/xhtml/glFrustum.Xml
	A = (right+left)/(right-left);
	B = (top+bottom)/(top-bottom);
	C = -(zFar+zNear)/(zFar-zNear);
	D = -(2.0*zFar*zNear)/(zFar-zNear);

	hmm_mat4 ret = {{
		{ (2.0*zNear)/(right-left), 0, 0, 0 }, // first *column*
		{ 0, (2.0*zNear)/(top-bottom), 0, 0 },
		{ A, B, C, -1.0 },
		{ 0, 0, D, 0 }
	}};

	return ret;
}

hmm_mat4 GL3_Perspective2(GLdouble fovy, GLdouble aspect, GLdouble zoom, GLdouble nearClip, GLdouble farClip)
{
	float h = (1.0 / tan(fovy * M_PI / 180.0 * 0.5)) * zoom;
	float w = h / aspect;
        float q = farClip / (farClip - nearClip);
        float r = -q * nearClip;

	float D = -(2.0*farClip*nearClip)/(farClip-nearClip);

	hmm_mat4 ret = {{
		{ w, 0, 0, 0 },
		{ 0, h, 0, 0 },
		{ 0, 0, 2.0f*q-1.0f, 2.0f*r },
		{ 0, 0, D, 0 }
	}};
	return ret;
}
