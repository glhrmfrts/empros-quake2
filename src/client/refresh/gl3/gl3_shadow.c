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
	-0.5f, 0.5f, 0.5f, 1.5f,
	0.5f, 0.5f, 0.5f, 0.5f,
	-0.5f, 0.5f, 1.5f, 1.5f,
	-0.5f, -0.5f, 1.5f, 0.5f,
	0.5f, 0.5f, 2.5f, 1.5f,
	-0.5f, 0.5f, 2.5f, 0.5f
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
#if 0
	gl3_shadow_light_t* light;
	gl3_shadow_light_t* next;

	for (light = gl3state.first_shadow_light; light; light = next)
	{
		GL3_DestroyFramebuffer(&light->shadow_map_fbo);
		next = light->next;
		free(light);
	}

	gl3state.first_shadow_light = NULL;
	gl3state.current_shadow_light = NULL;
	gl3state.last_shadow_light_rendered = NULL;
#endif
}

void GL3_Shadow_BeginFrame()
{
	shadowLightFrameCount = 0;
	GL3_Shadow_InitAllocator(SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE, SHADOW_ATLAS_SIZE);
}

static void AnglesForCubeFace(int index, vec3_t angles)
{
	static const vec3_t pointLightDirection[] = {
		{1.0f, 0.0f, 0.0f}, // Positive X
		{-1.0f, 0.0f, 0.0f}, // Negative X
		{0.0f, 1.0f, 0.0f}, // Positive Y
		{0.0f, -1.0f, 0.0f}, // Negative Y
		{0.0f, 0.0f, 1.0f}, // Positive Z
		{0.0f, 0.0f, -1.0f}, // Negative Z
	};

	vec3_t dir;
	VectorCopy(pointLightDirection[index], dir);
	AngleVectors2(dir, angles);
}

static void SetupShadowView(gl3_shadow_light_t* light, int viewIndex)
{
	gl3_shadow_view_t* view = &light->shadowViews[viewIndex];

	// first put Z axis going up
	hmm_mat4 viewMat = {{
		{  0, 0, -1, 0 }, // first *column* (the matrix is colum-major)
		{ -1, 0,  0, 0 },
		{  0, 1,  0, 0 },
		{  0, 0,  0, 1 }
	}};

	if (viewIndex == 1)
	{
		viewMat = (hmm_mat4){{
			{  0, 0, -1, 0 }, // first *column* (the matrix is colum-major)
			{ 1, 0,  0, 0 },
			{  0, -1,  0, 0 },
			{  0, 0,  0, 1 }
		}};
	}

	vec3_t angles = {0};
	if (light->type == gl3_shadow_light_type_point)
		AnglesForCubeFace(viewIndex, angles);
	else
		VectorCopy(light->light_angles, angles);

	// now rotate by view angles
	hmm_mat4 rotMat = rotAroundAxisXYZ(angles[2], angles[1], angles[0]);

	viewMat = HMM_MultiplyMat4( viewMat, rotMat );

	// .. and apply translation for current position
	hmm_vec3 trans = HMM_Vec3(-light->light_position[0], -light->light_position[1], -light->light_position[2]);
	view->viewMatrix = HMM_MultiplyMat4( viewMat, HMM_Translate(trans) );
	view->projMatrix = GL3_MYgluPerspective(light->coneangle, 1.0f, 10.0f, light->radius*3.0f);
}

void GL3_Shadow_AddDynLight(int dlightIndex, const vec3_t pos, float intensity)
{
	gl3_shadow_light_t* l = &shadowLights[shadowLightFrameCount++];
	memset(l, 0, sizeof(gl3_shadow_light_t));
	l->id = shadowLightFrameCount - 1;
	l->dlightIndex = dlightIndex;
	l->type = gl3_shadow_light_type_point;
	l->bias = POINT_SHADOW_BIAS;
	l->radius = intensity;
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

/*
void GL3_Shadow_AddSpotLight(
	const vec3_t origin,
	const vec3_t angles,
	const vec3_t color,
	float coneangle,
	float zfar,
	int resolution,
	float intensity,
	qboolean is_static)
{
	if (!zfar) {
		zfar = 600;
	}

	gl3_shadow_light_t* l = calloc(1, sizeof(gl3_shadow_light_t));
	l->id = shadow_light_id_gen++;
	l->enabled = true;
	l->cast_shadow = true;
	l->type = gl3_shadow_light_type_spot;
	l->bias = SPOT_SHADOW_BIAS;
	l->coneangle = coneangle;
	l->coneouterangle = coneangle + 20.0f;
	l->radius = zfar;
	l->shadow_map_width = SPOT_SHADOW_WIDTH;
	l->shadow_map_height = SPOT_SHADOW_HEIGHT;
	l->intensity = intensity;
	l->is_static = is_static;

	VectorCopy(origin, l->light_position);
	VectorCopy(color, l->light_color);

	l->light_angles[0] = -angles[1];
	l->light_angles[1] = -angles[0];
	l->light_angles[2] = angles[2];

	vec3_t fwd, right, up;
	AngleVectors(angles, fwd, right, up);
	l->light_normal[0] = fwd[0];
	l->light_normal[1] = fwd[1];
	l->light_normal[2] = fwd[2];

	const gl3_framebuffer_flag_t fbo_flags = GL3_FRAMEBUFFER_FILTERED | GL3_FRAMEBUFFER_DEPTH | GL3_FRAMEBUFFER_SHADOWMAP;
	GL3_CreateFramebuffer(SPOT_SHADOW_WIDTH, SPOT_SHADOW_HEIGHT, 1, fbo_flags, &l->shadow_map_fbo);

	l->next = gl3state.first_shadow_light;
	gl3state.first_shadow_light = l;

	R_Printf(PRINT_ALL, "Added shadow spotlight %f %f %f - %f %f %f.\n",
		l->light_position[0], l->light_position[1], l->light_position[2],
		l->light_angles[0], l->light_angles[1], l->light_angles[2]);
}
*/

static void AddLightToUniformBuffer(const gl3_shadow_light_t* light)
{
	float shadowStrength = 0.5f;

	gl3UniDynLight* dlight = &gl3state.uniLightsData.dynLights[light->dlightIndex];
	dlight->shadowParameters = HMM_Vec4(0.5f / (float)SHADOW_ATLAS_SIZE, 0.5f / (float)SHADOW_ATLAS_SIZE, shadowStrength, 0.0f);

	float nearClip = 10.0f;
	float farClip = light->radius*3.0f;
	float q = farClip / (farClip - nearClip);
	float r = -q * nearClip;
	float zoom = 1;

	int shadowMapSize = light->shadowMapHeight/2;
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

	/* build the transformation matrix for the given view angles */
	// VectorCopy(gl3_newrefdef.vieworg, light->light_position);
	// AngleVectors(gl3_newrefdef.viewangles, vpn, vright, vup);

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

	gl3state.uni3DData.transViewMat4 = view->viewMatrix;
	gl3state.uni3DData.transProjMat4 = view->projMatrix;
	GL3_UpdateUBO3D();

/*
	// vec3_t fwd, right, up;
	// AngleVectors(light->light_angles, fwd, right, up);
	// VectorCopy(fwd, light->light_normal);
	//hmm_vec3 vorg = HMM_Vec3(light->light_position[0], light->light_position[1], light->light_position[2]);
	//hmm_vec3 vfwd = HMM_Vec3(fwd[0], fwd[1], fwd[2]);
*/
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
	}

	// TODO: no need to bind if no shadows

	glActiveTexture(GL3_FACE_SELECTION1_TU);
	glBindTexture(GL_TEXTURE_CUBE_MAP, faceSelectionTex1);

	glActiveTexture(GL3_FACE_SELECTION2_TU);
	glBindTexture(GL_TEXTURE_CUBE_MAP, faceSelectionTex2);

	gl3state.current_shadow_light = NULL;

	GL3_InvalidateTextureBindings();
}