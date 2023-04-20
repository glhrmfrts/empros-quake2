#include "../ref_shared.h"
#include "header/local.h"
#include <float.h>

#define eprintf(...)  R_Printf(PRINT_ALL, __VA_ARGS__)

#define SUN_SHADOW_BIAS (0.01f)

#define SPOT_SHADOW_BIAS (0.000001f)

#define POINT_SHADOW_BIAS SPOT_SHADOW_BIAS

#define DEFAULT_SLOPE_SCALE_BIAS 1.5f
#define DEFAULT_DEPTH_BIAS 2.0f

cvar_t* r_flashlight;
cvar_t* r_shadowmap;
cvar_t* r_shadowmap_maxlights;

qboolean gl3_rendering_shadow_maps;

static int shadow_light_id_gen;

static gl3_framebuffer_t shadowAtlasFbo;
static gl3_framebuffer_t staticAtlasFbo;

static int shadowLightFrameCount;
static gl3ShadowLight_t shadowLights[MAX_SHADOW_LIGHTS];

static int staticLightsCount;
static gl3ShadowLight_t staticLights[MAX_STATIC_SHADOW_LIGHTS];

static area_allocator_t shadowMapAllocator;
static area_allocator_t staticMapAllocator;

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

static int shadowMapResolutionConfig;

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

static void SetResolutionConfig()
{
	shadowMapResolutionConfig = 0;
	if (r_shadowmap->value == 1.0f)
	{
		shadowMapResolutionConfig = 256;
	}
	else if (r_shadowmap->value == 2.0f)
	{
		shadowMapResolutionConfig = 512;
	}
}

void GL3_Shadow_Init()
{
	// Since the static atlas is never sampled directly for shadows,
	// we don't need to use the SHADOWMAP flag

	const gl3_framebuffer_flag_t fboFlags = GL3_FRAMEBUFFER_FILTERED | GL3_FRAMEBUFFER_DEPTH | GL3_FRAMEBUFFER_SHADOWMAP;
	GL3_CreateFramebuffer(SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE, 1, fboFlags, &shadowAtlasFbo);
	GL3_CreateFramebuffer(SHADOW_STATIC_ATLAS_SIZE, SHADOW_STATIC_ATLAS_SIZE, 1, fboFlags & ~GL3_FRAMEBUFFER_SHADOWMAP, &staticAtlasFbo);

	CreateFaceSelectionTexture(faceSelectionData1, &faceSelectionTex1);
	CreateFaceSelectionTexture(faceSelectionData2, &faceSelectionTex2);

	AreaAlloc_Init(&staticMapAllocator,
		SHADOW_STATIC_ATLAS_SIZE, SHADOW_STATIC_ATLAS_SIZE, SHADOW_STATIC_ATLAS_SIZE, SHADOW_STATIC_ATLAS_SIZE);

	SetResolutionConfig();

	GL3_BindFramebuffer(&staticAtlasFbo, true);
	GL3_UnbindFramebuffer();

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

static qboolean SetupPointLight(gl3ShadowLight_t* l, int shadowMapResolution, area_allocator_t* alloc);

void GL3_Shadow_BeginFrame()
{
	if (r_shadowmap->value && !inited)
		GL3_Shadow_Init();

	shadowLightFrameCount = 0;
	AreaAlloc_Init(&shadowMapAllocator, SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE);

	// Sanitize the values
	r_shadowmap_maxlights->value = min(r_shadowmap_maxlights->value, 32);
	r_shadowmap_maxlights->value = max(r_shadowmap_maxlights->value, 1);

	SetResolutionConfig();
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

static void SetupShadowView(gl3ShadowLight_t* light, int viewIndex)
{
	gl3ShadowView_t* view = &light->shadowViews[viewIndex];

	vec3_t angles = { 0 };
	if (light->type == SHADOW_LIGHT_TYPE_POINT)
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

int GL3_Shadow_CreateStaticLight(const vec3_t pos, float radius, gl3ShadowMode_t mode)
{
	if (staticLightsCount >= MAX_STATIC_SHADOW_LIGHTS) return false;

	gl3ShadowLight_t* l = &staticLights[staticLightsCount++];
	memset(l, 0, sizeof(gl3ShadowLight_t));
	l->id = staticLightsCount - 1;
	l->type = SHADOW_LIGHT_TYPE_POINT;
	l->bias = POINT_SHADOW_BIAS;
	l->radius = radius*2.0f;
	l->coneAngle = 90;
	l->mode = mode;
	l->staticMapInvalidated = true;
	VectorCopy(pos, l->position);

	if (!SetupPointLight(l, shadowMapResolutionConfig, &staticMapAllocator))
	{
		staticLightsCount - 1;
		return -1;
	}

	return staticLightsCount - 1;
}

static qboolean CullShadowLight(const vec3_t pos, float radius, qboolean isMapLight, int* resolution)
{
	static const float thresholds[][3] = {
		{400.0f, 700.0f, 1000.0f}, // for dyn lights
		{500.0f, 1000.0f, 2000.0f}, // for map lights
	};

	// The further away from the view, the smaller the shadow map can be

	vec3_t lightToView = { 0 };
	VectorSubtract(pos, gl3_newrefdef.vieworg, lightToView);
	float distanceSqr = DotProduct(lightToView, lightToView);

	distanceSqr -= radius*radius*0.5f;

	const float* thresh = thresholds[isMapLight ? 1 : 0];
	int shadowMapResolution = shadowMapResolutionConfig;
	if (distanceSqr > (thresh[0] * thresh[0]))
		shadowMapResolution /= 2;
	if (distanceSqr > (thresh[1] * thresh[1]))
		shadowMapResolution /= 2;
	if (distanceSqr > (thresh[2] * thresh[2]))
		return true; // Too far away and Quake 2 doesn't have big dynamic lights, so don't do shadows

	*resolution = shadowMapResolution;
	return false;
}

qboolean GL3_Shadow_TouchStaticLight(int dlightIndex, int staticLightIndex)
{
	gl3ShadowLight_t* staticLight = &staticLights[staticLightIndex];

	if (!r_shadowmap->value || !shadowMapResolutionConfig)
		return false;

	if (shadowLightFrameCount >= (int)r_shadowmap_maxlights->value)
		return false;

	int resolution = 0;
	if (CullShadowLight(staticLight->position, staticLight->radius, true, &resolution))
		return false;

	// Progressively render the static lights at startup
	if (staticLight->staticMapInvalidated && !((gl3_framecount % staticLightsCount) == staticLight->id))
		return false;

	gl3ShadowLight_t* frameLight = &shadowLights[shadowLightFrameCount++];
	*frameLight = *staticLight;
	frameLight->id = shadowLightFrameCount - 1;
	frameLight->staticOwner = staticLight;
	frameLight->dlightIndex = dlightIndex;
	if (!SetupPointLight(frameLight, resolution, &shadowMapAllocator))
	{
		shadowLightFrameCount--;
	}
	return true;
}

qboolean GL3_Shadow_AddDynLight(int dlightIndex, const vec3_t pos, float radius, qboolean isMapLight, gl3ShadowMode_t mode)
{
	if (!r_shadowmap->value || !shadowMapResolutionConfig)
		return false;

	if (shadowLightFrameCount >= (int)r_shadowmap_maxlights->value)
		return false;

	// The further away from the view, the smaller the shadow map can be
	int shadowMapResolution = 0;
	if (CullShadowLight(pos, radius, isMapLight, &shadowMapResolution))
		return false;

	gl3ShadowLight_t* l = &shadowLights[shadowLightFrameCount++];
	memset(l, 0, sizeof(gl3ShadowLight_t));
	l->id = shadowLightFrameCount - 1;
	l->dlightIndex = dlightIndex;
	l->mode = mode;
	l->type = SHADOW_LIGHT_TYPE_POINT;
	l->bias = POINT_SHADOW_BIAS;
	l->radius = radius*2.0f;
	l->coneAngle = 90;
	VectorCopy(pos, l->position);

	if (!SetupPointLight(l, shadowMapResolution, &shadowMapAllocator))
	{
		shadowLightFrameCount--;
	}

	return true;
}

static qboolean SetupPointLight(gl3ShadowLight_t* l, int shadowMapResolution, area_allocator_t* alloc)
{
	// Allocate room for this light in the shadow map atlas
	l->shadowMapWidth = shadowMapResolution * 3;
	l->shadowMapHeight = shadowMapResolution * 2;
	if (!AreaAlloc_Allocate(alloc, l->shadowMapWidth, l->shadowMapHeight, &l->shadowMapX, &l->shadowMapY))
	{
		return false;
	}

	l->numShadowViews = 6;
	for (int i = 0; i < l->numShadowViews; i++)
	{
		SetupShadowView(l, i);
	}

	return true;
}

static void AddLightToUniformBuffer(const gl3ShadowLight_t* light)
{
	float shadowStrength = 0.5f;

	gl3UniDynLight* dlight = &gl3state.uniLightsData.dynLights[light->dlightIndex];
	dlight->shadowParameters = HMM_Vec4(1.0f / (float)SHADOW_ATLAS_SIZE, 1.0f / (float)SHADOW_ATLAS_SIZE, shadowStrength, 0.0f);

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

static void ShadowViewCoords(gl3ShadowLight_t* light, int viewIndex, int* x, int* y, int* width, int* height)
{
	int actualShadowMapSize = light->shadowMapHeight / 2;
	int smx = light->shadowMapX;
	int smy = light->shadowMapY;
	if (viewIndex & 1) smy += actualShadowMapSize;
	smx += ((unsigned)viewIndex >> 1) * actualShadowMapSize;
	*x = smx;
	*y = smy;
	*width = *height = actualShadowMapSize;
}

static void PrepareToRender(gl3ShadowLight_t* light, int viewIndex)
{
	gl3ShadowView_t* view = &light->shadowViews[viewIndex];

	// Make the FOV a bit bigger for culling

	vec3_t angles;
	VectorCopy(view->angles, angles);

	// TODO: fix this mess
	if (viewIndex == 0)
	{
		GL3_SetViewParams(light->position, angles, light->coneAngle * 1.2f, light->coneAngle * 1.2f, light->radius * 0.01f, light->radius, 1.0f);
	}
	else if (viewIndex == 1)
	{
		//angles[1] -= 90;
		GL3_SetViewParams(light->position, angles, light->coneAngle * 1.2f, light->coneAngle * 1.2f, light->radius * 0.01f, light->radius, 1.0f);
	}
	else if (viewIndex == 2)
	{
		GL3_SetViewParams(light->position, angles, light->coneAngle * 1.2f, light->coneAngle * 1.2f, light->radius * 0.01f, light->radius, 1.0f);
	}
	else if (viewIndex == 3)
	{
		angles[0] = -angles[0];
		GL3_SetViewParams(light->position, angles, light->coneAngle*1.2f, light->coneAngle*1.2f, light->radius*0.01f, light->radius, 1.0f);
	}
	else if (viewIndex == 4)
	{
		angles[0] -= 90;
		angles[1] -= 90;
		GL3_SetViewParams(light->position, angles, light->coneAngle * 1.2f, light->coneAngle * 1.2f, light->radius * 0.01f, light->radius, 1.0f);
	}
	else
	{
		angles[0] -= 90;
		angles[1] -= 90;
		GL3_SetViewParams(light->position, angles, light->coneAngle*1.2f, light->coneAngle*1.2f, light->radius*0.01f, light->radius, 1.0f);
	}

	//GL3_Debug_AddFrustum((const vec3_t) { 0, 1, 0 });

	GL3_SetupViewCluster();
	GL3_MarkLeaves();

	if (light->type == SHADOW_LIGHT_TYPE_POINT)
	{
		int smx, smy, width, height;
		if (light->staticOwner)
		{
			ShadowViewCoords(light->staticOwner, viewIndex, &smx, &smy, &width, &height);
		}
		else
		{
			ShadowViewCoords(light, viewIndex, &smx, &smy, &width, &height);
		}
		glViewport(smx, smy, width, height);
		//glScissor(smx, smy, width, height);
		//glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
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

static void RenderShadowMap(gl3ShadowLight_t* light)
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
		if (light->mode == SHADOWMODE_DYNAMIC)
		{
			GL3_DrawEntitiesOnList();
		}
	}
}

static void BlitStaticShadowMap(gl3ShadowLight_t* light)
{
	GL3_UseProgram(gl3state.siShadowMapBlit.shaderProgram);

	GL3_BindFramebufferDepthTexture(&staticAtlasFbo, 0);

	for (int viewIndex = 0; viewIndex < light->numShadowViews; viewIndex++)
	{
		int smx, smy, width, height;
		ShadowViewCoords(light->staticOwner, viewIndex, &smx, &smy, &width, &height);

		int destx, desty, destw, desth;
		ShadowViewCoords(light, viewIndex, &destx, &desty, &destw, &desth);
		glViewport(destx, desty, destw, desth);

		const float u = (float)smx / SHADOW_STATIC_ATLAS_SIZE;
		const float v = (float)smy / SHADOW_STATIC_ATLAS_SIZE;
		const float u2 = u + ((float)width / SHADOW_STATIC_ATLAS_SIZE);
		const float v2 = v + ((float)height / SHADOW_STATIC_ATLAS_SIZE);

		GL3_Draw_TexRect(-1.0f, -1.0f, 1.0f, 1.0f, u, v, u2, v2);
	}
}

static void UpdateLight(gl3ShadowLight_t* light)
{
	if (light->staticOwner)
	{
		if (light->staticOwner->staticMapInvalidated)
		{
			GL3_BindFramebuffer(&staticAtlasFbo, false);
			RenderShadowMap(light);
			light->staticOwner->staticMapInvalidated = false;
		}
		GL3_BindFramebuffer(&shadowAtlasFbo, false);
		BlitStaticShadowMap(light);
	}
	else
	{
		RenderShadowMap(light);
	}
}

void GL3_Shadow_RenderShadowMaps()
{
	if (!r_shadowmap->value)
		return false;

	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(DEFAULT_SLOPE_SCALE_BIAS, DEFAULT_DEPTH_BIAS);

	// Unbind the shadow atlas before rendering to it
	GL3_SelectTMU(GL3_SHADOW_ATLAS_TU);
	glBindTexture(GL_TEXTURE_2D, 0);

	gl3state.renderPass = RENDER_PASS_SHADOW;

	if (!shadowDebug)
	{
		GL3_BindFramebuffer(&shadowAtlasFbo, true);
	}

	glEnable(GL_DEPTH_TEST);

	hmm_mat4 old_view = gl3state.uni3DData.transViewMat4;
	hmm_mat4 old_proj = gl3state.uni3DData.transProjMat4;
	gl3ViewParams_t oldViewParams = gl3state.viewParams;

	gl3state.lastShadowLightRendered = NULL;

	for (int i = 0; i < shadowLightFrameCount; i++)
	{
		gl3ShadowLight_t* light = &shadowLights[i];
		gl3state.currentShadowLight = light;

		UpdateLight(light);

		gl3state.lastShadowLightRendered = light;
		AddLightToUniformBuffer(light);
	}

	// At least one shadow map was rendered?
	if (gl3state.lastShadowLightRendered)
	{
		GL3_UnbindFramebuffer();

		// Restore original 3D params
		gl3state.viewParams = oldViewParams;

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

	glDisable(GL_POLYGON_OFFSET_FILL);
}
