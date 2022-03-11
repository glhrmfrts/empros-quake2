#include "../ref_shared.h"
#include "header/local.h"
#include "header/HandmadeMath.h"

// TODO: framebuffer pool

enum {
	SUN_SHADOW_WIDTH = 1024*4,
	SUN_SHADOW_HEIGHT = 1024*4,

	SPOT_SHADOW_WIDTH = 1024*2,
	SPOT_SHADOW_HEIGHT = 1024*2,

	POINT_SHADOW_WIDTH = 1024,
	POINT_SHADOW_HEIGHT = 1024,
};

#define SUN_SHADOW_BIAS (0.01f)

#define SPOT_SHADOW_BIAS (0.000001f)

#define POINT_SHADOW_BIAS SPOT_SHADOW_BIAS

cvar_t* r_flashlight;

qboolean gl3_rendering_shadow_maps;

static int shadow_light_id_gen;

void GL3_Shadow_Init()
{
	vec3_t origin = {0};
	vec3_t angles = {0};
	GL3_Shadow_AddSpotLight(origin, angles, 70.0f, 600.0f);
	gl3state.flashlight = gl3state.first_shadow_light;
	gl3state.flashlight->cast_shadow = false; // until we have better shadows
}

void GL3_Shadow_Shutdown()
{
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

void GL3_Shadow_AddSpotLight(const vec3_t origin, const vec3_t angles, float coneangle, float zfar)
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

	VectorCopy(origin, l->light_position);

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

static void AddLightToUniformBuffer(const gl3_shadow_light_t* light)
{
	if (gl3state.uniShadowsData.num_shadow_maps >= MAX_FRAME_SHADOWS) {
		R_Printf(PRINT_ALL, "WARNING: Shadow map limit reached, max: %d\n", MAX_FRAME_SHADOWS);
		return;
	}

	gl3UniShadowSingle_t* ldata = &gl3state.uniShadowsData.shadows[gl3state.uniShadowsData.num_shadow_maps++];
	ldata->light_type = (int)light->type;
	ldata->brighten = light->brighten;
	ldata->darken = light->darken;
	ldata->bias = light->bias;
	ldata->radius = light->radius;
	ldata->spot_cutoff = cosf(HMM_ToRadians(light->coneangle * 0.5f));
	ldata->spot_outer_cutoff = cosf(HMM_ToRadians(light->coneouterangle * 0.5f));
	VectorCopy(light->light_position, ((float*)&ldata->light_position));
	VectorCopy(light->light_normal, ((float*)&ldata->light_normal));
	ldata->view_matrix = light->view_matrix;
	ldata->proj_matrix = light->proj_matrix;
	ldata->cast_shadow = (int)light->cast_shadow;

	int idx = gl3state.uniShadowsData.num_shadow_maps - 1;
	if (false)
	{//(light->type == gl3_shadow_light_type_point) {
		//gl3state.shadow_frame_textures[gl3state.uniShadowsData.num_shadow_maps - 1].id = light->shadow_map_cubemap;
		//gl3state.shadow_frame_textures[gl3state.uniShadowsData.num_shadow_maps - 1].unit = SHADOW_MAP_TEXTURE_UNIT - GL_TEXTURE0 + light->id;
	}
    else
	{
		gl3state.shadow_frame_textures[idx].texnum = light->shadow_map_fbo.depth_texture;
		gl3state.shadow_frame_textures[idx].unit = GL3_SHADOW_MAP_TEXTURE_UNIT - GL_TEXTURE0 + light->id;
	}
}

static void AddLightsToUBO()
{
	gl3state.uniShadowsData.use_shadow = (int)1;//r_shadow_enabled.value;
	gl3state.uniShadowsData.num_shadow_maps = 0;

	for (gl3_shadow_light_t* light = gl3state.first_shadow_light; light; light = light->next)
	{
		if (light->rendered)
		{
			AddLightToUniformBuffer(light);
		}
	}

	GL3_UpdateUBOShadows();
}

static void PrepareToRender(gl3_shadow_light_t* light)
{
	SetupShadowViewCluster(light);
	GL3_MarkLeaves();
	if (false)
	{
		glViewport(0, 0, 1024, 1024);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}
	else
	{
		GL3_BindFramebuffer(&light->shadow_map_fbo);
		
		if (false)
		{   //(light->type == r_shadow_light_type_point) {
			// GL_FramebufferTexture2DFunc (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			// 	shadow_point_camera_directions[light->current_cube_face].cubemap_face,
			// 	light->shadow_map_cubemap, 0);
			// glDrawBuffer(GL_COLOR_ATTACHMENT0);
			// glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		}
	}

	// vec3_t fwd, right, up;
	// AngleVectors(light->light_angles, fwd, right, up);
	// VectorCopy(fwd, light->light_normal);

	//hmm_vec3 vorg = HMM_Vec3(light->light_position[0], light->light_position[1], light->light_position[2]);
	//hmm_vec3 vfwd = HMM_Vec3(fwd[0], fwd[1], fwd[2]);

	// first put Z axis going up
	hmm_mat4 viewMat = {{
		{  0, 0, -1, 0 }, // first *column* (the matrix is colum-major)
		{ -1, 0,  0, 0 },
		{  0, 1,  0, 0 },
		{  0, 0,  0, 1 }
	}};

	// now rotate by view angles
	hmm_mat4 rotMat = rotAroundAxisXYZ(light->light_angles[2], light->light_angles[1], light->light_angles[0]);

	viewMat = HMM_MultiplyMat4( viewMat, rotMat );

	// .. and apply translation for current position
	hmm_vec3 trans = HMM_Vec3(-light->light_position[0], -light->light_position[1], -light->light_position[2]);
	light->view_matrix = HMM_MultiplyMat4( viewMat, HMM_Translate(trans) );
	light->proj_matrix = GL3_MYgluPerspective(light->coneangle, (float)gl3_newrefdef.width / (float)gl3_newrefdef.height, 1.0f, light->radius);
}

void GL3_Shadow_SetupLightShader(gl3_shadow_light_t* light)
{
}

static void RenderSpotShadowMap(gl3_shadow_light_t* light)
{
	//light->brighten = r_shadow_sunbrighten.value;
	//light->darken = r_shadow_sundarken.value;
	light->brighten = 0.7;
	light->darken = 0.8;

	PrepareToRender(light);

	entity_t ent = {0};
	ent.frame = (int)(gl3_newrefdef.time * 2);
	GL3_RecursiveWorldNode(&ent, gl3_worldmodel->nodes, light->light_position);

	gl3state.uni3DData.transViewMat4 = light->view_matrix;
	gl3state.uni3DData.transProjMat4 = light->proj_matrix;
	GL3_UpdateUBO3D();

	GL3_UseProgram(gl3state.siShadowMap.shaderProgram);
	GL3_DrawTextureChains(&ent);
	GL3_DrawEntitiesOnList();
}

static qboolean CullLight(const gl3_shadow_light_t* light)
{
	return true;
}

void GL3_Shadow_RenderShadowMaps()
{
	glEnable(GL_DEPTH_TEST);

	hmm_mat4 old_view = gl3state.uni3DData.transViewMat4;
	hmm_mat4 old_proj = gl3state.uni3DData.transProjMat4;

	gl3state.last_shadow_light_rendered = NULL;

	for (gl3_shadow_light_t* light = gl3state.first_shadow_light; light; light = light->next)
	{
		light->rendered = false;

		if (!light->enabled) { continue; }

		if (!light->cast_shadow)
		{
			light->rendered = true;
			continue;
		}

		if (CullLight(light))
		{
			gl3state.current_shadow_light = light;
			RenderSpotShadowMap(light);
			light->rendered = true;
			gl3state.last_shadow_light_rendered = light;
		}
	}

	AddLightsToUBO();

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

		for (int i = 0; i < gl3state.uniShadowsData.num_shadow_maps; i++)
		{
			GL3_BindShadowmap(i);
		}
	}

	gl3state.current_shadow_light = NULL;
}