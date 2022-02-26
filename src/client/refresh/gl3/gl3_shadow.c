#include "../ref_shared.h"
#include "header/local.h"
#include "header/HandmadeMath.h"

enum { MAX_FRAME_SHADOWS = 10 };

typedef enum r_shadow_light_type {
	//r_shadow_light_type_sun,
	r_shadow_light_type_spot,
	//r_shadow_light_type_point,
} r_shadow_light_type_t;

typedef struct r_shadow_light_s {
	int id;
	r_shadow_light_type_t type;
	qboolean enabled;
	qboolean rendered; // rendered this frame?
	vec3_t light_position;
	vec3_t light_normal;
	vec3_t light_angles; // (pitch yaw roll)
	float brighten;
	float darken;
	float radius;
	float bias;
	int shadow_map_width;
	int shadow_map_height;
	GLuint shadow_map_fbo;
	GLuint shadow_map_texture;
	// GLuint shadow_map_cubemap;
	GLenum current_cube_face;
	hmm_mat4 world_to_shadow_map;
	hmm_mat4 shadow_map_projview[6];

	// next in the global list of lights
	struct r_shadow_light_s* next;
} r_shadow_light_t;

typedef struct {
	hmm_mat4 shadow_matrix;
	hmm_vec4 light_normal;
	hmm_vec4 light_position;
	float brighten;
	float darken;
	float radius;
	float bias;
	float spot_cutoff;
	int light_type;
	int pad1;
	int pad2;
} shadow_ubo_single_t;

typedef struct {
	int use_shadow;
	int num_shadow_maps;
	int pad1;
	int pad2;
	shadow_ubo_single_t shadows[MAX_FRAME_SHADOWS];
} shadow_ubo_data_t;

qboolean gl3_rendering_shadow_maps;
static r_shadow_light_t* first_light;
static r_shadow_light_t* last_light_rendered;
static int light_id_gen;

static GLuint shadow_ubo;
static shadow_ubo_data_t shadow_ubo_data;

static void SetupShadowViewCluster(const r_shadow_light_t* light)
{
	int i;
	mleaf_t *leaf;

	gl3_framecount++;

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

		leaf = GL3_Mod_PointInLeaf(light->light_position, gl3_worldmodel);

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

static void UpdateShadowUBO()
{
    if (!shadow_ubo) {
		glGenBuffers (1, &shadow_ubo);
		glBindBuffer (GL_UNIFORM_BUFFER, shadow_ubo);
		glBufferData (GL_UNIFORM_BUFFER, sizeof(shadow_ubo_data_t), &shadow_ubo_data, GL_DYNAMIC_DRAW);
		glBindBuffer (GL_UNIFORM_BUFFER, 0);
		glBindBufferBase (GL_UNIFORM_BUFFER, SHADOW_UBO_BINDING_POINT, shadow_ubo);
	}

	shadow_ubo_data.use_shadow = (int)r_shadow_enabled.value;
	shadow_ubo_data.num_shadow_maps = 0;

	for (r_shadow_light_t* light = first_light; light; light = light->next) {
		if (light->rendered) {
			GL3_Shadow_AddLightToUniformBuffer (light);
		}
	}

	glBindBuffer (GL_UNIFORM_BUFFER, shadow_ubo);
	glBufferData (GL_UNIFORM_BUFFER, sizeof(shadow_ubo_data_t), &shadow_ubo_data, GL_DYNAMIC_DRAW);
	glBindBuffer (GL_UNIFORM_BUFFER, 0);
}

static void AddLightToUniformBuffer(const r_shadow_light_t* light)
{
	if (shadow_ubo_data.num_shadow_maps >= MAX_FRAME_SHADOWS) {
		Con_DWarning ("Shadow map limit reached, max: %d\n", MAX_FRAME_SHADOWS);
		return;
	}

	shadow_ubo_single_t* ldata = &shadow_ubo_data.shadows[shadow_ubo_data.num_shadow_maps++];
	ldata->light_type = (int)light->type;
	ldata->brighten = light->brighten;
	ldata->darken = light->darken;
	ldata->bias = light->bias;
	ldata->radius = light->radius;
	ldata->spot_cutoff = 0.3f;
	VectorCopy (light->light_position, ((float*)&ldata->light_position));
	VectorCopy (light->light_position, ((float*)&ldata->light_normal));
	memcpy(&ldata->shadow_matrix, &light->world_to_shadow_map, sizeof(hmm_mat4));

	if (light->type == r_shadow_light_type_point) {
		shadow_frame_textures[shadow_ubo_data.num_shadow_maps - 1].id = light->shadow_map_cubemap;
		shadow_frame_textures[shadow_ubo_data.num_shadow_maps - 1].unit = SHADOW_MAP_TEXTURE_UNIT - GL_TEXTURE0 + light->id;
	}
    else {
        shadow_frame_textures[shadow_ubo_data.num_shadow_maps - 1].id = light->shadow_map_texture;
        shadow_frame_textures[shadow_ubo_data.num_shadow_maps - 1].unit = SHADOW_MAP_TEXTURE_UNIT - GL_TEXTURE0 + light->id;   
    }
}

static void PrepareToRender(r_shadow_light_t* light)
{
	MarkSurfacesForLightShadowMap(light);
	if (r_shadow_debug.value) {
		glViewport (0, 0, 512, 512);
		glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}
	else {
		glBindFramebuffer (GL_FRAMEBUFFER, light->shadow_map_fbo);
		glViewport (0, 0, light->shadow_map_width, light->shadow_map_height);
		
		if (light->type == r_shadow_light_type_point) {
			GL_FramebufferTexture2DFunc (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
				shadow_point_camera_directions[light->current_cube_face].cubemap_face,
				light->shadow_map_cubemap, 0);
			glDrawBuffer (GL_COLOR_ATTACHMENT0);
			glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		}
		else {
			glClear (GL_DEPTH_BUFFER_BIT);
		}
	}
}

static void RenderSpotShadowMap(r_shadow_light_t* light)
{
    light->brighten = r_shadow_sunbrighten.value;
	light->darken = r_shadow_sundarken.value;

	PrepareToRender(light);
	DrawTextureChains(light, gl3_worldmodel, NULL, chain_world);
	DrawEntities(light);

	last_light_rendered = light;
	light->rendered = true;
}

void GL3_Shadow_RenderShadowMaps()
{
    last_light_rendered = NULL;
    for (r_shadow_light_t* light = first_light; light; light = light->next) {
        if (CullLight(light)) {
            RenderSpotShadowMap(light);
            last_light_rendered = light;
        }
    }

    if (last_light_rendered) {
        UpdateShadowUBO();

        // Restore the original viewport

        glDrawBuffer(GL_FRONT_AND_BACK);
    }
}
