#include "../ref_shared.h"
#include "header/local.h"
#include "header/HandmadeMath.h"
#include <float.h>

#define eprintf(...)  R_Printf(PRINT_ALL, __VA_ARGS__)

// TODO: fix culling alias entities
// TODO: integrate this new system with the mapper-designed shadow lights

#define SUN_SHADOW_BIAS (0.01f)

#define SPOT_SHADOW_BIAS (0.000001f)

#define POINT_SHADOW_BIAS SPOT_SHADOW_BIAS

cvar_t* r_flashlight;
cvar_t* r_shadowmap;
cvar_t* r_shadowmap_resolution;
cvar_t* r_shadowmap_maxlights;

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

static qboolean inited;

void GL3_Shadow_Init()
{
	const gl3_framebuffer_flag_t fbo_flags = GL3_FRAMEBUFFER_FILTERED | GL3_FRAMEBUFFER_DEPTH | GL3_FRAMEBUFFER_SHADOWMAP;
	GL3_CreateFramebuffer(SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE, 1, fbo_flags, &shadowAtlasFbo);
	CreateFaceSelectionTexture(faceSelectionData1, &faceSelectionTex1);
	CreateFaceSelectionTexture(faceSelectionData2, &faceSelectionTex2);
	inited = true;
}

void GL3_Shadow_Shutdown()
{
	if (!inited) return;

#if 0
	GL3_DestroyFramebuffer(&shadowAtlasFbo);
	if (faceSelectionTex1 != 0) glDeleteTextures(1, &faceSelectionTex1);
	if (faceSelectionTex2 != 0) glDeleteTextures(1, &faceSelectionTex2);
#endif
}

void GL3_Shadow_BeginFrame()
{
	if (r_shadowmap->value && !inited)
		GL3_Shadow_Init();

	shadowLightFrameCount = 0;
	GL3_Shadow_InitAllocator(SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE);

	// Sanitize the values
	r_shadowmap_maxlights->value = min(r_shadowmap_maxlights->value, 32);
	r_shadowmap_maxlights->value = max(r_shadowmap_maxlights->value, 1);

	r_shadowmap_resolution->value = min(r_shadowmap_resolution->value, DEFAULT_SHADOWMAP_SIZE);
	r_shadowmap_resolution->value = max(r_shadowmap_resolution->value, 32);
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
	DirectionToAngles(dir, angles);
}

static void SetupShadowView(gl3_shadow_light_t* light, int viewIndex)
{
	gl3_shadow_view_t* view = &light->shadowViews[viewIndex];

	vec3_t angles = { 0 };
	if (light->type == gl3_shadow_light_type_point)
		AnglesForCubeFace(viewIndex, angles);
	else
		VectorCopy(light->angles, angles);

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
	hmm_vec3 trans = HMM_Vec3(-light->position[0], -light->position[1], -light->position[2]);
	view->viewMatrix = HMM_MultiplyMat4( viewMat, HMM_Translate(trans) );

	int shadowMapSize = light->shadowMapHeight / 2;
	float zoom = (float)(shadowMapSize - 4) / (float)shadowMapSize;
	view->projMatrix = GL3_MYgluPerspective(light->coneAngle, 1.0f, zoom, light->radius*0.01f, light->radius);

	VectorCopy(angles, view->angles);
}

qboolean GL3_Shadow_AddDynLight(int dlightIndex, const vec3_t pos, float intensity)
{
	if (!r_shadowmap->value)
		return false;

	if (shadowLightFrameCount >= (int)r_shadowmap_maxlights->value)
		return false;

	// The further away from the view, the smaller the shadow map can be
	vec3_t lightToView = { 0 };
	VectorSubtract(pos, gl3_newrefdef.vieworg, lightToView);
	float distanceSqr = DotProduct(lightToView, lightToView);

	int shadowMapResolution = (int)r_shadowmap_resolution->value;
	if (distanceSqr > (400.0f * 400.0f))
		shadowMapResolution /= 2;
	if (distanceSqr > (700.0f * 700.0f))
		shadowMapResolution /= 2;
	if (distanceSqr > (1000.0f * 1000.0f))
		return false; // Too far away and Quake 2 doesn't have big dynamic lights, so don't do shadows

	gl3_shadow_light_t* l = &shadowLights[shadowLightFrameCount++];
	memset(l, 0, sizeof(gl3_shadow_light_t));
	l->id = shadowLightFrameCount - 1;
	l->dlightIndex = dlightIndex;
	l->type = gl3_shadow_light_type_point;
	l->bias = POINT_SHADOW_BIAS;
	l->radius = intensity*2.0f;
	l->coneAngle = 90;
	VectorCopy(pos, l->position);

	l->shadowMapWidth = shadowMapResolution * 3;
	l->shadowMapHeight = shadowMapResolution * 2;
	if (!GL3_Shadow_Allocate(l->shadowMapWidth, l->shadowMapHeight, &l->shadowMapX, &l->shadowMapY))
	{
		shadowLightFrameCount--;
		return false;
	}

	l->numShadowViews = 6;
	for (int i = 0; i < l->numShadowViews; i++)
	{
		SetupShadowView(l, i);
	}

	return true;
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
		{  light->position[0], light->position[1],  light->position[2], 1 },
		{  0, 0,  0, 0 }
	} };
}

qboolean shadowDebug = false;

static void PrepareToRender(gl3_shadow_light_t* light, int viewIndex)
{
	gl3_shadow_view_t* view = &light->shadowViews[viewIndex];

	// Make the FOV a bit bigger for culling
	GL3_SetViewParams(light->position, view->angles, light->coneAngle*1.2f, light->coneAngle*1.2f);
	GL3_SetupViewCluster();
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
		gl3state.currentShadowLight = light;
		PrepareToRender(light, i);

		entity_t ent = {0};
		ent.frame = (int)(gl3_newrefdef.time * 2);
		GL3_RecursiveWorldNode(&ent, gl3_worldmodel->nodes, light->position);
		GL3_DrawTextureChainsShadowPass(&ent);
		GL3_DrawEntitiesOnList();
	}
}

void GL3_Shadow_RenderShadowMaps()
{
	if (!r_shadowmap->value)
		return false;

	// Unbind the shadow atlas before rendering to it
	GL3_SelectTMU(GL3_SHADOW_ATLAS_TU);
	glBindTexture(GL_TEXTURE_2D, 0);

	gl3state.renderPass = RENDER_PASS_SHADOW;

	if (!shadowDebug)
	{
		GL3_BindFramebuffer(&shadowAtlasFbo);
	}

	glViewport(0, 0, SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);

	hmm_mat4 old_view = gl3state.uni3DData.transViewMat4;
	hmm_mat4 old_proj = gl3state.uni3DData.transProjMat4;
	gl3ViewParams_t oldViewParams = gl3state.viewParams;

	gl3state.lastShadowLightRendered = NULL;

	for (int i = 0; i < shadowLightFrameCount; i++)
	{
		gl3_shadow_light_t* light = &shadowLights[i];
		gl3state.currentShadowLight = light;
		RenderShadowMap(light);
		gl3state.lastShadowLightRendered = light;
		AddLightToUniformBuffer(light);
	}

	// At least one shadow map was rendered?
	if (gl3state.lastShadowLightRendered)
	{
		GL3_UnbindFramebuffer();

		// Restore original 3D params
		gl3state.viewParams = oldViewParams;
		GL3_SetupViewCluster();
		GL3_MarkLeaves();

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

	gl3state.currentShadowLight = NULL;

	GL3_InvalidateTextureBindings();
}
