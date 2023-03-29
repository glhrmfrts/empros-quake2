#include "../ref_shared.h"
#include "header/local.h"
#include "header/HandmadeMath.h"
#include <float.h>

#define eprintf(...)  R_Printf(PRINT_ALL, __VA_ARGS__)

// TODO: scale dlight shadowmap size according to distance from view

enum {
	MAX_SHADOW_LIGHTS = 16,
	DEFAULT_SHADOWMAP_SIZE = 256,
	SHADOW_ATLAS_SIZE = 2048,
};

#define SUN_SHADOW_BIAS (0.01f)

#define SPOT_SHADOW_BIAS (0.000001f)

#define POINT_SHADOW_BIAS SPOT_SHADOW_BIAS

cvar_t* r_flashlight;

qboolean gl3_rendering_shadow_maps;

static int shadow_light_id_gen;

static gl3_framebuffer_t shadowAtlasFbo;
static gl3_shadow_light_t shadowLights[MAX_SHADOW_LIGHTS];
static int shadowLightFrameCount;

static const float faceSelectionData1[] = {
	1.0f, 0.0f, 0.0f, 0.0f,
	1.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 1.0f, 0.0f, 0.0f,
	0.0f, 1.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 1.0f, 0.0f,
	0.0f, 0.0f, 1.0f, 0.0f
};

static const float faceSelectionData2[] = {
        -0.5f, -0.5f, 0.5f, 1.5f,
        0.5f, -0.5f, 0.5f, 0.5f,

		-0.5f, 0.5f, 1.5f, 1.5f,
		-0.5f, -0.5f, 1.5f, 0.5f,

		0.5f, -0.5f, 2.5f, 1.5f,
		-0.5f, -0.5f, 2.5f, 0.5f
};

static GLuint faceSelectionTex1;
static GLuint faceSelectionTex2;

enum { MAX_CUBE_FACES = 6 };

static void CreateFaceSelectionTexture(const float* data, GLuint* texid)
{
	glGenTextures(1, texid);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, *texid);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	for (int fi = 0; fi < MAX_CUBE_FACES; fi++)
	{
		GLenum target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)fi;
		const float* faceData = data + (fi * 4);
		glTexImage2D(target, 0, GL_RGBA32F, 1, 1, 0, GL_RGBA, GL_FLOAT, (const void*)faceData);
	}
}

void GL3_Shadow_Init()
{
	const gl3_framebuffer_flag_t fbo_flags = GL3_FRAMEBUFFER_FILTERED | GL3_FRAMEBUFFER_DEPTH | GL3_FRAMEBUFFER_SHADOWMAP;
	GL3_CreateFramebuffer(SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE, 1, fbo_flags, &shadowAtlasFbo);
	CreateFaceSelectionTexture(faceSelectionData1, &faceSelectionTex1);
	CreateFaceSelectionTexture(faceSelectionData2, &faceSelectionTex2);
}

void GL3_Shadow_Shutdown()
{
	/*
	GL3_DestroyFramebuffer(&shadowAtlasFbo);
	glDeleteTextures(1, &faceSelectionTex1);
	glDeleteTextures(1, &faceSelectionTex2);
	*/
}

void GL3_Shadow_BeginFrame()
{
	shadowLightFrameCount = 0;
	GL3_Shadow_InitAllocator(SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE);
}

static const vec3_t pointLightDirection[] = {
	{1.0f, 0.0f, 0.0f}, // Positive X
	{-1.0f, 0.0f, 0.0f}, // Negative X
	{0.0f, 1.0f, 0.0f}, // Positive Y
	{0.0f, -1.0f, 0.0f}, // Negative Y
	{0.0f, 0.0f, 1.0f}, // Positive Z
	{0.0f, 0.0f, -1.0f}, // Negative Z
};

static void AnglesForCubeFace(int index, vec3_t angles)
{
	vec3_t dir;
	VectorCopy(pointLightDirection[index], dir);
	AngleVectors2(dir, angles);
}

static void SetupShadowView(gl3_shadow_light_t* light, int viewIndex)
{
	gl3_shadow_view_t* view = &light->shadowViews[viewIndex];

	vec3_t angles = { 0 };
	if (light->type == gl3_shadow_light_type_point)
		AnglesForCubeFace(viewIndex, angles);
	else
		VectorCopy(light->light_angles, angles);

	// first put Z axis going up
	hmm_mat4 viewMat = {{
		{  0, 0, -1, 0 }, // first *column* (the matrix is colum-major)
		{ -1, 0,  0, 0 },
		{  0, 1,  0, 0 },
		{  0, 0,  0, 1 }
	}};

	// This is probably dumb. I just messed with the view angles
	// to get the shadow rendering to work correctly.
#if 1
	if (viewIndex == 1)
	{
		viewMat = (hmm_mat4){{
			{  0, 0, -1, 0 }, // first *column* (the matrix is colum-major)
			{ 1, 0,  0, 0 },
			{  0, -1,  0, 0 },
			{  0, 0,  0, 1 }
		}};
	}
	if (viewIndex == 2)
	{
		viewMat = (hmm_mat4){ {
			{  0, 0, -1, 0 }, // first *column* (the matrix is colum-major)
			{ -1, 0,  0, 0 },
			{  0, -1,  0, 0 },
			{  0, 0,  0, 1 }
		} };
		angles[0] -= 90;
	}
	if (viewIndex == 3)
	{
		viewMat = (hmm_mat4){ {
			{  0, 0, -1, 0 }, // first *column* (the matrix is colum-major)
			{ -1, 0,  0, 0 },
			{  0, -1,  0, 0 },
			{  0, 0,  0, 1 }
		} };
		angles[0] -= 90;
	}
#endif

	// now rotate by view angles
	hmm_mat4 rotMat = rotAroundAxisXYZ(angles[2], angles[1], angles[0]);

	viewMat = HMM_MultiplyMat4( viewMat, rotMat );

	// .. and apply translation for current position
	hmm_vec3 trans = HMM_Vec3(-light->light_position[0], -light->light_position[1], -light->light_position[2]);
	view->viewMatrix = HMM_MultiplyMat4( viewMat, HMM_Translate(trans) );

	int shadowMapSize = light->shadowMapHeight / 2;
	float zoom = (float)(shadowMapSize - 4) / (float)shadowMapSize;
	view->projMatrix = GL3_MYgluPerspective(light->coneangle, 1.0f, zoom, light->radius*0.01f, light->radius);
}

void GL3_Shadow_AddDynLight(int dlightIndex, const vec3_t pos, float intensity)
{
	if (shadowLightFrameCount >= MAX_SHADOW_LIGHTS)
		return;

	gl3_shadow_light_t* l = &shadowLights[shadowLightFrameCount++];
	memset(l, 0, sizeof(gl3_shadow_light_t));
	l->id = shadowLightFrameCount - 1;
	l->dlightIndex = dlightIndex;
	l->type = gl3_shadow_light_type_point;
	l->bias = POINT_SHADOW_BIAS;
	//l->radius = intensity;
	l->radius = intensity*2.0f;
	l->coneangle = 90;
	VectorCopy(pos, l->light_position);

	l->shadowMapWidth = DEFAULT_SHADOWMAP_SIZE * 3;
	l->shadowMapHeight = DEFAULT_SHADOWMAP_SIZE * 2;
	GL3_Shadow_Allocate(l->shadowMapWidth, l->shadowMapHeight, &l->shadowMapX, &l->shadowMapY);

	l->numShadowViews = 6;
	for (int i = 0; i < l->numShadowViews; i++)
	{
		SetupShadowView(l, i);
	}
}

static void AddLightToUniformBuffer(const gl3_shadow_light_t* light)
{
	float shadowStrength = 0.5f;

	gl3UniDynLight* dlight = &gl3state.uniLightsData.dynLights[light->dlightIndex];
	dlight->shadowParameters = HMM_Vec4(0.5f / (float)SHADOW_ATLAS_SIZE, 0.5f / (float)SHADOW_ATLAS_SIZE, shadowStrength, 0.0f);

	float nearClip = light->radius*0.01f;
	float farClip = light->radius;
	float q = farClip / (farClip - nearClip);
	float r = -q * nearClip;

	int shadowMapSize = light->shadowMapHeight/2;
	float zoom = (float)(shadowMapSize - 4) / (float)shadowMapSize;

	dlight->shadowMatrix = (hmm_mat4){ {
		{  (float)shadowMapSize / SHADOW_ATLAS_SIZE, (float)shadowMapSize / SHADOW_ATLAS_SIZE, (float)light->shadowMapX / SHADOW_ATLAS_SIZE, (float)light->shadowMapY / SHADOW_ATLAS_SIZE },
		{ zoom, q,  r, 0 },
		{  light->light_position[0], light->light_position[1],  light->light_position[2], 1 },
		{  0, 0,  0, 0 }
	} };
}

static void AddShadowsToUBO()
{
	for (int i = 0; i < shadowLightFrameCount; i++)
	{
		AddLightToUniformBuffer(shadowLights + i);
	}
}

static void SetupShadowViewCluster(const gl3_shadow_light_t* light)
{
	int i;
	mleaf_t *leaf;

	/* current viewcluster */
	if (!(gl3_newrefdef.rdflags & RDF_NOWORLDMODEL))
	{
		gl3_oldviewcluster = gl3_viewcluster;
		gl3_oldviewcluster2 = gl3_viewcluster2;
		leaf = GL3_Mod_PointInLeaf(light->light_position, gl3_worldmodel);
		gl3_viewcluster = gl3_viewcluster2 = leaf->cluster;

		//R_Printf(PRINT_ALL, "light cluster = %d\n", leaf->cluster);

		/* check above and below so crossing solid water doesn't draw wrong */
		if (!leaf->contents)
		{
			/* look down a bit */
			vec3_t temp;

			VectorCopy(light->light_position, temp);
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

			VectorCopy(light->light_position, temp);
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

qboolean shadowDebug = false;

static void PrepareToRender(gl3_shadow_light_t* light, int viewIndex)
{
	gl3_shadow_view_t* view = &light->shadowViews[viewIndex];

	SetupShadowViewCluster(light);
	GL3_MarkLeaves();

	if (light->type == gl3_shadow_light_type_point)
	{
		int actualShadowMapSize = light->shadowMapHeight / 2;
		int smx = light->shadowMapX;
		int smy = light->shadowMapY;
		if (viewIndex & 1) smy += actualShadowMapSize;
		smx += ((unsigned)viewIndex >> 1) * actualShadowMapSize;
		glViewport(smx, smy, actualShadowMapSize, actualShadowMapSize);
	}
	else
	{
		glViewport(light->shadowMapX, light->shadowMapY, light->shadowMapWidth, light->shadowMapHeight);
	}

	vec3_t dir;
	VectorCopy(pointLightDirection[viewIndex], dir);

	gl3state.uni3DData.transViewMat4 = view->viewMatrix;
	gl3state.uni3DData.transProjMat4 = view->projMatrix;
	gl3state.uni3DData.fogParams = (hmm_vec4){dir[0], dir[1], dir[2]};
	GL3_UpdateUBO3D();
}

static void RenderShadowMap(gl3_shadow_light_t* light)
{
	GL3_UseProgram(gl3state.siShadowMap.shaderProgram);

	for (int i = 0; i < light->numShadowViews; i++)
	{
		PrepareToRender(light, i);

		entity_t ent = {0};
		ent.frame = (int)(gl3_newrefdef.time * 2);
		GL3_RecursiveWorldNode(&ent, gl3_worldmodel->nodes, light->light_position);
		GL3_DrawTextureChains(&ent);

		//GL3_DrawEntitiesOnList();
	}
}

static qboolean CullLight(const gl3_shadow_light_t* light)
{
	return true;
}

void GL3_Shadow_RenderShadowMaps()
{
	// Unbind the shadow atlas before rendering to it
	GL3_SelectTMU(GL3_SHADOW_ATLAS_TU);
	glBindTexture(GL_TEXTURE_2D, 0);

	gl3state.render_pass = RENDER_PASS_SHADOW;

	if (!shadowDebug)
	{
		GL3_BindFramebuffer(&shadowAtlasFbo);
	}

	glViewport(0, 0, SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);

	hmm_mat4 old_view = gl3state.uni3DData.transViewMat4;
	hmm_mat4 old_proj = gl3state.uni3DData.transProjMat4;

	gl3state.last_shadow_light_rendered = NULL;

	for (int i = 0; i < shadowLightFrameCount; i++)
	{
		gl3_shadow_light_t* light = &shadowLights[i];
		if (CullLight(light))
		{
			gl3state.current_shadow_light = light;
			RenderShadowMap(light);
			gl3state.last_shadow_light_rendered = light;
		}
	}

	AddShadowsToUBO();

	// At least one shadow map was rendered?
	if (gl3state.last_shadow_light_rendered)
	{
		GL3_UnbindFramebuffer();

		// Restore the original viewport
		// glDrawBuffer(GL_FRONT_AND_BACK);
		// Restore the original view cluster (from the player POV)
		// GL3_SetupViewCluster();

		// Restore original 3D params
		gl3state.uni3DData.transViewMat4 = old_view;
		gl3state.uni3DData.transProjMat4 = old_proj;
		GL3_UpdateUBO3D();

		GL3_BindFramebufferDepthTexture(&shadowAtlasFbo, GL3_SHADOW_ATLAS_TU - GL_TEXTURE0);
		GL3_BindFramebufferTexture(&shadowAtlasFbo, 0, GL3_SHADOW_DEBUG_COLOR_TU - GL_TEXTURE0);

		glActiveTexture(GL3_FACE_SELECTION1_TU);
		glBindTexture(GL_TEXTURE_CUBE_MAP, faceSelectionTex1);

		glActiveTexture(GL3_FACE_SELECTION2_TU);
		glBindTexture(GL_TEXTURE_CUBE_MAP, faceSelectionTex2);
	}

	gl3state.current_shadow_light = NULL;

	GL3_InvalidateTextureBindings();
}
