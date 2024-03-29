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
 * Local header for the OpenGL3 refresher.
 *
 * =======================================================================
 */


#ifndef SRC_CLIENT_REFRESH_GL3_HEADER_LOCAL_H_
#define SRC_CLIENT_REFRESH_GL3_HEADER_LOCAL_H_

#ifdef IN_IDE_PARSER
  // this is just a hack to get proper auto-completion in IDEs:
  // using system headers for their parsers/indexers but glad for real build
  // (in glad glFoo is just a #define to glad_glFoo or sth, which screws up autocompletion)
  // (you may have to configure your IDE to #define IN_IDE_PARSER, but not for building!)
  #define GL_GLEXT_PROTOTYPES 1
  #include <GL/gl.h>
  #include <GL/glext.h>
#else
  #include "../glad/include/glad/glad.h"
#endif

#include "../../ref_shared.h"

#include "../../../common/header/HandmadeMath.h"

#if 0 // only use this for development ..
#define STUB_ONCE(msg) do { \
		static int show=1; \
		if(show) { \
			show = 0; \
			R_Printf(PRINT_ALL, "STUB: %s() %s\n", __FUNCTION__, msg); \
		} \
	} while(0);
#else // .. so make this a no-op in released code
#define STUB_ONCE(msg)
#endif

// a wrapper around glVertexAttribPointer() to stay sane
// (caller doesn't have to cast to GLintptr and then void*)
static inline void
qglVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, GLintptr offset)
{
	glVertexAttribPointer(index, size, type, normalized, stride, (const void*)offset);
}

static inline void
qglVertexAttribIPointer(GLuint index, GLint size, GLenum type, GLsizei stride, GLintptr offset)
{
	glVertexAttribIPointer(index, size, type, stride, (void*)offset);
}

// attribute locations for vertex shaders
enum {
	GL3_ATTRIB_POSITION   = 0,
	GL3_ATTRIB_TEXCOORD   = 1, // for normal texture
	GL3_ATTRIB_LMTEXCOORD = 2, // for lightmap
	GL3_ATTRIB_COLOR      = 3, // per-vertex color
	GL3_ATTRIB_NORMAL     = 4, // vertex normal
	GL3_ATTRIB_LIGHTFLAGS = 5,  // uint, each set bit means "dyn light i affects this surface"
	GL3_ATTRIB_STYLE0     = 6,
	GL3_ATTRIB_STYLE1     = 7,
	GL3_ATTRIB_STYLE2     = 8,
	GL3_ATTRIB_STYLE3     = 9,
};

enum {
	GL3_BINDINGPOINT_UNICOMMON,
	GL3_BINDINGPOINT_UNI2D,
	GL3_BINDINGPOINT_UNI3D,
	GL3_BINDINGPOINT_UNILIGHTS,
	GL3_BINDINGPOINT_UNISTYLES,
	GL3_BINDINGPOINT_UNISHADOWS,
	GL3_BINDINGPOINT_UNISSAO,
};

// TODO: do we need the following configurable?
static const int gl3_solid_format = GL_RGB;
static const int gl3_alpha_format = GL_RGBA;
static const int gl3_tex_solid_format = GL_RGB;
static const int gl3_tex_alpha_format = GL_RGBA;

extern unsigned gl3_rawpalette[256];
extern unsigned d_8to24table[256];

extern hmm_vec2 gl3_scaledSize;

typedef struct
{
	const char *renderer_string;
	const char *vendor_string;
	const char *version_string;
	const char *glsl_version_string;

	int major_version;
	int minor_version;

	// ----

	qboolean anisotropic; // is GL_EXT_texture_filter_anisotropic supported?
	qboolean debug_output; // is GL_ARB_debug_output supported?
	qboolean stencil; // Do we have a stencil buffer?

	qboolean useBigVBO; // workaround for AMDs windows driver for fewer calls to glBufferData()

	// ----

	float max_anisotropy;
} gl3config_t;

typedef struct
{
	GLuint shaderProgram;
	GLint uniLmScales;
	hmm_vec4 lmScales[4];
} gl3ShaderInfo_t;

typedef struct
{
	GLfloat gamma;
	GLfloat intensity;
	GLfloat intensity2D; // for HUD, menus etc

		// entries of std140 UBOs are aligned to multiples of their own size
		// so we'll need to pad accordingly for following vec4
		GLfloat _padding;

	hmm_vec4 color;
} gl3UniCommon_t;

typedef struct
{
	hmm_mat4 transMat4;
} gl3Uni2D_t;

typedef struct
{
	hmm_mat4 transProjMat4;
	hmm_mat4 transViewMat4;
	hmm_mat4 transInverseView;
	hmm_mat4 transModelMat4;

	float specularStrength;
	float shininess;
	float _pad01;
	float _pad02;

	GLfloat scroll; // for SURF_FLOWING
	GLfloat time; // for warping surfaces like water & possibly other things
	GLfloat alpha; // for translucent surfaces (water, glass, ..)
	GLfloat emission; // for light surfaces
	GLfloat overbrightbits; // gl3_overbrightbits, applied to lightmaps (and elsewhere to models)
	GLfloat particleFadeFactor; // gl3_particle_fade_factor, higher => less fading out towards edges
	GLfloat ssao;

	GLfloat _padding[1]; // again, some padding to ensure this has right size

	hmm_vec4 fogParams; // RGB + density
} gl3Uni3D_t;

extern const hmm_mat4 gl3_identityMat4;

typedef struct
{
	vec3_t origin;
	GLfloat attenuation;
	vec3_t color;
	GLfloat intensity;
	hmm_vec4 shadowParameters;
	hmm_mat4 shadowMatrix;
} gl3UniDynLight;

typedef struct
{
	gl3UniDynLight dynLights[MAX_DLIGHTS];
	GLuint numDynLights;
	GLfloat _padding[3];
} gl3UniLights_t;

typedef struct
{
	hmm_vec4 lightstyles[MAX_LIGHTSTYLES];
} gl3UniStyles_t;

enum {
	MAX_SHADOW_LIGHTS = 32,
	MAX_STATIC_SHADOW_LIGHTS = 256,
	DEFAULT_SHADOWMAP_SIZE = 512,
	SHADOW_ATLAS_SIZE = 4096,
	SHADOW_STATIC_ATLAS_SIZE = 8192,
};

enum {
	GL3_SHADOW_ATLAS_TU = GL_TEXTURE5,
	GL3_SHADOW_DEBUG_COLOR_TU = GL_TEXTURE6,
	GL3_FACE_SELECTION1_TU = GL_TEXTURE7,
	GL3_FACE_SELECTION2_TU = GL_TEXTURE8,
	GL3_SSAO_MAP_TU = GL_TEXTURE15,
};

typedef struct {
	hmm_mat4 view_matrix;
	hmm_mat4 proj_matrix;
	hmm_vec4 light_normal;
	hmm_vec4 light_position;
	hmm_vec4 light_color;
	float intensity;
	float darken;
	float radius;
	float bias;
	float spot_cutoff;
	float spot_outer_cutoff;
	int light_type;
	int cast_shadow;
} gl3UniShadowSingle_t;

enum {
	// width and height used to be 128, so now we should be able to get the same lightmap data
	// that used 32 lightmaps before into one, so 4 lightmaps should be enough
	BLOCK_WIDTH = 1024,
	BLOCK_HEIGHT = 512,
	LIGHTMAP_BYTES = 4,
	MAX_LIGHTMAPS = 16,
	MAX_LIGHTMAPS_PER_SURFACE = MAXLIGHTMAPS // 4
};

struct gl3_3D_vtx_s;
struct gl3_shadow_light_s;

typedef enum gl3RenderPass {
	RENDER_PASS_SHADOW,
	RENDER_PASS_SSAO,
	RENDER_PASS_SCENE,
} gl3RenderPass_t;

typedef struct {
	hmm_vec4 vertices[8];
	cplane_t frustum[4];
	vec3_t angles;
	vec3_t vup;
	vec3_t vforward;
	vec3_t vright;
	vec3_t origin;
	float fovX;
	float fovY;
} gl3ViewParams_t;

typedef struct
{
	// TODO: what of this do we need?
	qboolean fullscreen;

	int prev_mode;

	// each lightmap consists of 4 sub-lightmaps allowing changing shadows on the same surface
	// used for switching on/off light and stuff like that.
	// most surfaces only have one really and the remaining for are filled with dummy data
	GLuint lightmap_textureIDs[MAX_LIGHTMAPS][MAX_LIGHTMAPS_PER_SURFACE]; // instead of lightmap_textures+i use lightmap_textureIDs[i]

	GLuint currenttexture; // bound to GL_TEXTURE0
	int currentlightmap; // lightmap_textureIDs[currentlightmap] bound to GL_TEXTURE1
	GLuint currenttmu; // GL_TEXTURE0 or GL_TEXTURE1

	gl3RenderPass_t renderPass;

	//float camera_separation;
	//enum stereo_modes stereo_mode;

	GLuint currentVAO;
	GLuint currentVBO;
	GLuint currentEBO;
	GLuint currentShaderProgram;
	GLuint currentUBO;

	// NOTE: make sure si2D is always the first shaderInfo (or adapt GL3_ShutdownShaders())
	gl3ShaderInfo_t si2D;      // shader for rendering 2D with textures
	gl3ShaderInfo_t si2Dcolor; // shader for rendering 2D with flat colors
	gl3ShaderInfo_t si3Dlm;        // a regular opaque face (e.g. from brush) with lightmap
	gl3ShaderInfo_t si3DlmTurb;    // a regular opaque water face (e.g. from brush) with lightmap

	// TODO: lm-only variants for gl_lightmap 1
	gl3ShaderInfo_t si3Dtrans;     // transparent is always w/o lightmap
	gl3ShaderInfo_t si3DcolorOnly; // used for beams - no lightmaps
	gl3ShaderInfo_t si3Dturb;      // for water etc - always without lightmap
	gl3ShaderInfo_t si3DlmFlow;    // for flowing/scrolling things with lightmap (conveyor, ..?)
	gl3ShaderInfo_t si3DtransFlow; // for transparent flowing/scrolling things (=> no lightmap)
	gl3ShaderInfo_t si3Dsky;       // guess what..
	gl3ShaderInfo_t si3Dsprite;    // for sprites
	gl3ShaderInfo_t si3DspriteAlpha; // for sprites with alpha-testing

	gl3ShaderInfo_t si3Dalias;      // for models
	gl3ShaderInfo_t si3DaliasColor; // for models w/ flat colors

	gl3ShaderInfo_t si3DSSAO;

	gl3ShaderInfo_t siParticle; // for particles. surprising, right?

	gl3ShaderInfo_t siPostfxResolveMultisample;
	gl3ShaderInfo_t siPostfxResolveHDR;
	gl3ShaderInfo_t siPostfxSSAO;
	gl3ShaderInfo_t siPostfxSSAOBlur;
	gl3ShaderInfo_t siPostfxMotionBlur;
	gl3ShaderInfo_t siPostfxBloomFilter;
	gl3ShaderInfo_t siPostfxBloomBlur;
	gl3ShaderInfo_t siPostfxBlend;
	gl3ShaderInfo_t siPostfxUnderwater;

	gl3ShaderInfo_t siShadowMap;
	gl3ShaderInfo_t siShadowMapBlit;

	gl3ShaderInfo_t si3Ddebug;

	gl3ShaderInfo_t siSentinel;

	GLuint vao3D, vbo3D; // for brushes etc, using 10 floats and one uint as vertex input (x,y,z, s,t, lms,lmt, normX,normY,normZ ; lightFlags)
	GLuint vaoWorld, vboWorld; // for static world geometry

	struct gl3_3D_vtx_s* world_vertices;
	unsigned int num_world_vertices;
	unsigned int cap_world_vertices;

	int lightmap_step;

	qboolean postfx_initialized;

	// the next two are for gl3config.useBigVBO == true
	int vbo3Dsize;
	int vbo3DcurOffset;

	GLuint vaoAlias, vboAlias, eboAlias; // for models, using 9 floats as (x,y,z, s,t, r,g,b,a)
	GLuint vaoParticle, vboParticle; // for particles, using 9 floats (x,y,z, size,distance, r,g,b,a)

	// UBOs and their data
	gl3UniCommon_t uniCommonData;
	gl3Uni2D_t uni2DData;
	gl3Uni3D_t uni3DData;
	gl3UniLights_t uniLightsData;
	gl3UniStyles_t uniStylesData;
	GLuint uniCommonUBO;
	GLuint uni2DUBO;
	GLuint uni3DUBO;
	GLuint uniLightsUBO;
	GLuint uniStylesUBO;

	gl3ViewParams_t viewParams;

	struct gl3ShadowLight* lastShadowLightRendered;
	struct gl3ShadowLight* currentShadowLight;
	struct gl3ShadowLight* flashlight;
} gl3state_t;

extern gl3config_t gl3config;
extern gl3state_t gl3state;

extern viddef_t vid;

extern refdef_t gl3_newrefdef;

extern int gl3_visframecount; /* bumped when going to a new PVS */
extern int gl3_framecount; /* used for dlight push checking */

extern int gl3_viewcluster, gl3_viewcluster2, gl3_oldviewcluster, gl3_oldviewcluster2;

extern int c_brush_polys, c_alias_polys;

/* NOTE: struct image_s* is what re.RegisterSkin() etc return so no gl3image_s!
 *       (I think the client only passes the pointer around and doesn't know the
 *        definition of this struct, so this being different from struct image_s
 *        in ref_gl should be ok)
 */
typedef struct image_s
{
	char name[MAX_QPATH];               /* game path, including extension */
	imagetype_t type;
	int width, height;                  /* source image */
	//int upload_width, upload_height;    /* after power of two and picmip */
	int registration_sequence;          /* 0 = free */
	struct msurface_s *texturechain;    /* for sort-by-texture world drawing */
	GLuint texnum;                      /* gl texture binding */
	float sl, tl, sh, th;               /* 0,0 - 1,1 unless part of the scrap */
	// qboolean scrap; // currently unused
	qboolean has_alpha;

} gl3image_t;

enum {MAX_GL3TEXTURES = 1024};

// include this down here so it can use gl3image_t
#include "model.h"

typedef struct
{
	int internal_format;
	int current_lightmap_texture; // index into gl3state.lightmap_textureIDs[]

	//msurface_t *lightmap_surfaces[MAX_LIGHTMAPS]; - no more lightmap chains, lightmaps are rendered multitextured

	int allocated[BLOCK_WIDTH];

	/* the lightmap texture data needs to be kept in
	   main memory so texsubimage can update properly */
	byte lightmap_buffers[MAX_LIGHTMAPS_PER_SURFACE][4 * BLOCK_WIDTH * BLOCK_HEIGHT];
} gl3lightmapstate_t;

extern gl3model_t *gl3_worldmodel;

extern float gl3depthmin, gl3depthmax;

extern gl3image_t *gl3_notexture; /* use for bad textures */
extern gl3image_t *gl3_particletexture; /* little dot for particles */

extern int gl_filter_min;
extern int gl_filter_max;

static inline void
GL3_UseProgram(GLuint shaderProgram)
{
	if(shaderProgram != gl3state.currentShaderProgram)
	{
		gl3state.currentShaderProgram = shaderProgram;
		glUseProgram(shaderProgram);
	}
}

static inline void
GL3_BindVAO(GLuint vao)
{
	if(vao != gl3state.currentVAO)
	{
		gl3state.currentVAO = vao;
		glBindVertexArray(vao);
	}
}

static inline void
GL3_BindVBO(GLuint vbo)
{
	if(vbo != gl3state.currentVBO)
	{
		gl3state.currentVBO = vbo;
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
	}
}

static inline void
GL3_BindEBO(GLuint ebo)
{
	if(ebo != gl3state.currentEBO)
	{
		gl3state.currentEBO = ebo;
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
	}
}

// Yaw-Pitch-Roll
// equivalent to R_z * R_y * R_x where R_x is the trans matrix for rotating around X axis for aroundXdeg
static inline hmm_mat4 rotAroundAxisZYX(float aroundZdeg, float aroundYdeg, float aroundXdeg)
{
	// Naming of variables is consistent with http://planning.cs.uiuc.edu/node102.html
	// and https://de.wikipedia.org/wiki/Roll-Nick-Gier-Winkel#.E2.80.9EZY.E2.80.B2X.E2.80.B3-Konvention.E2.80.9C
	float alpha = HMM_ToRadians(aroundZdeg);
	float beta = HMM_ToRadians(aroundYdeg);
	float gamma = HMM_ToRadians(aroundXdeg);

	float sinA = HMM_SinF(alpha);
	float cosA = HMM_CosF(alpha);
	// TODO: or sincosf(alpha, &sinA, &cosA); ?? (not a standard function)
	float sinB = HMM_SinF(beta);
	float cosB = HMM_CosF(beta);
	float sinG = HMM_SinF(gamma);
	float cosG = HMM_CosF(gamma);

	hmm_mat4 ret = {{
		{ cosA*cosB,                  sinA*cosB,                   -sinB,    0 }, // first *column*
		{ cosA*sinB*sinG - sinA*cosG, sinA*sinB*sinG + cosA*cosG, cosB*sinG, 0 },
		{ cosA*sinB*cosG + sinA*sinG, sinA*sinB*cosG - cosA*sinG, cosB*cosG, 0 },
		{  0,                          0,                          0,        1 }
	}};

	return ret;
}

// equivalent to R_x * R_y * R_z where R_x is the trans matrix for rotating around X axis for aroundXdeg
static inline hmm_mat4 rotAroundAxisXYZ(float aroundXdeg, float aroundYdeg, float aroundZdeg)
{
	float alpha = HMM_ToRadians(aroundXdeg);
	float beta = HMM_ToRadians(aroundYdeg);
	float gamma = HMM_ToRadians(aroundZdeg);

	float sinA = HMM_SinF(alpha);
	float cosA = HMM_CosF(alpha);
	float sinB = HMM_SinF(beta);
	float cosB = HMM_CosF(beta);
	float sinG = HMM_SinF(gamma);
	float cosG = HMM_CosF(gamma);

	hmm_mat4 ret = {{
		{  cosB*cosG,  sinA*sinB*cosG + cosA*sinG, -cosA*sinB*cosG + sinA*sinG, 0 }, // first *column*
		{ -cosB*sinG, -sinA*sinB*sinG + cosA*cosG,  cosA*sinB*sinG + sinA*cosG, 0 },
		{  sinB,      -sinA*cosB,                   cosA*cosB,                  0 },
		{  0,          0,                           0,                          1 }
	}};

	return ret;
}

extern void GL3_BufferAndDraw3D(const gl3_3D_vtx_t* verts, int numVerts, GLenum drawMode);

// Returns true if the box is completely outside the view frustum
extern qboolean GL3_CullBox(const vec3_t mins, const vec3_t maxs);

// Returns true if the sphere is completely outside the view frustum
extern qboolean GL3_CullSphere(const vec3_t pos, float radius);

extern void GL3_RotateForEntity(entity_t *e);

extern hmm_mat4 GL3_MYgluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zoom, GLdouble zNear, GLdouble zFar);

extern hmm_mat4 GL3_Perspective2(GLdouble fovy, GLdouble aspect, GLdouble zoom, GLdouble nearClip, GLdouble farClip);

extern void GL3_DrawEntitiesOnList(void);

// gl3_fog.c

extern void GL3_Fog_Init(void);

// gnemeth: Command to set the renderer fog
// usage: fog <density> <red> <green> <blue> - Changes density and color
// usage: fog <density>                      - Changes density only
extern void GL3_Fog_f(void);

// gnemeth: Command to set the renderer fog smoothly, interpolates from current fog settings to new settings
// usage: foglerp <time> <density> <red> <green> <blue> - Changes density and color
// usage: foglerp <time> <density>                      - Changes density only
//
// You can cancel the fog lerping by calling the "fog" command again
extern void GL3_FogLerp_f(void);

extern void GL3_Fog_Set(float r, float g, float b, float d);
extern hmm_vec4 GL3_Fog_Params();

extern void GL3_Fog_SetupFrame(void);

// gl3_sdl.c
extern int GL3_InitContext(void* win);
extern int GL3_PrepareForWindow(void);
extern qboolean GL3_IsVsyncActive(void);
extern void GL3_EndFrame(void);
extern void GL3_SetVsync(void);
extern void GL3_ShutdownContext(void);

// gl3_misc.c
extern void GL3_InitParticleTexture(void);
extern void GL3_ScreenShot(void);
extern void GL3_SetDefaultState(void);

// gl3_model.c
extern int registration_sequence;
extern void GL3_Mod_Init(void);
extern void GL3_Mod_FreeAll(void);
extern void GL3_BeginRegistration(char *model);
extern struct model_s * GL3_RegisterModel(char *name);
extern void GL3_EndRegistration(void);
extern void GL3_Mod_Modellist_f(void);
extern const byte* GL3_Mod_ClusterPVS(int cluster, const gl3model_t *model);
extern mleaf_t* GL3_Mod_PointInLeaf(vec3_t p, gl3model_t *model);

// gl3_draw.c
extern void GL3_Draw_InitLocal(void);
extern void GL3_Draw_ShutdownLocal(void);
extern gl3image_t * GL3_Draw_FindPic(char *name);
extern void GL3_Draw_GetPicSize(int *w, int *h, char *pic);
extern int GL3_Draw_GetPalette(void);

// Bind texture before calling this
extern void GL3_Draw_TexRect(float x, float y, float x2, float y2, float u, float v, float u2, float v2);

extern void GL3_Draw_PicScaled(int x, int y, char *pic, float factor);
extern void GL3_Draw_StretchPic(int x, int y, int w, int h, char *pic);
extern void GL3_Draw_CharScaled(int x, int y, int num, float scale);
extern void GL3_Draw_TileClear(int x, int y, int w, int h, char *pic);
extern void GL3_Draw_Fill(int x, int y, int w, int h, int c);
extern void GL3_Draw_FadeScreen(void);
extern void GL3_Draw_Flash(const float color[4], float x, float y, float w, float h);
extern void GL3_Draw_StretchRaw(int x, int y, int w, int h, int cols, int rows, byte *data);

// gl3_image.c

static inline void
GL3_SelectTMU(GLenum tmu)
{
	if(gl3state.currenttmu != tmu)
	{
		glActiveTexture(tmu);
		gl3state.currenttmu = tmu;
	}
}

extern void GL3_TextureMode(char *string);
extern void GL3_Bind(GLuint texnum);
extern void GL3_BindLightmap(int lightmapnum);
extern void GL3_InvalidateTextureBindings();
extern gl3image_t *GL3_LoadPic(char *name, byte *pic, int width, int realwidth,
                               int height, int realheight, imagetype_t type, int bits);
extern gl3image_t *GL3_FindImage(char *name, imagetype_t type);
extern gl3image_t *GL3_RegisterSkin(char *name);
extern void GL3_ShutdownImages(void);
extern void GL3_FreeUnusedImages(void);
extern qboolean GL3_ImageHasFreeSpace(void);
extern void GL3_ImageList_f(void);

// gl3_light.c
extern void GL3_InitLights(void);
extern void GL3_MarkLights(dlight_t *light, int bit, mnode_t *node);
extern void GL3_AddMapLight(const vec3_t pos, const vec3_t color, float radius, float intensity, qboolean shadow);
extern void GL3_PushDlights(void);
extern void GL3_LightPoint(entity_t *currententity, vec3_t p, vec3_t color);
extern void GL3_BuildLightMap(msurface_t *surf, int offsetInLMbuf, int stride, int step);

// gl3_lightmap.c
#define GL_LIGHTMAP_FORMAT GL_RGBA

extern void GL3_LM_InitBlock(void);
extern void GL3_LM_UploadBlock(void);
extern qboolean GL3_LM_AllocBlock(int w, int h, int *x, int *y);
extern void GL3_LM_BuildPolygonFromSurface(gl3model_t *currentmodel, msurface_t *fa, int step);
extern void GL3_LM_BuildPolygonFromWarpSurface(gl3model_t *currentmodel, msurface_t *fa);
extern void GL3_LM_CreateSurfaceLightmap(msurface_t *surf, int step);
extern void GL3_LM_BeginBuildingLightmaps(gl3model_t *m);
extern void GL3_LM_EndBuildingLightmaps(void);

// gl3_warp.c
extern void GL3_EmitWaterPolys(msurface_t *fa);
extern void GL3_SubdivideSurface(msurface_t *fa, gl3model_t* loadmodel);

extern void GL3_SetSky(char *name, float rotate, vec3_t axis);
extern void GL3_DrawSkyBox(void);
extern void GL3_ClearSkyBox(void);
extern void GL3_AddSkySurface(msurface_t *fa);


// gl3_surf.c
extern void GL3_SurfInit(void);
extern void GL3_SurfShutdown(void);
extern void GL3_DrawGLPoly(msurface_t *fa);
extern void GL3_DrawGLFlowingPoly(msurface_t *fa);
extern void GL3_DrawTriangleOutlines(void);
extern void GL3_DrawAlphaSurfaces(void);
extern void GL3_DrawEmissiveSurfaces(void);
extern void GL3_DrawBrushModel(entity_t *e, gl3model_t *currentmodel);
extern void GL3_DrawWorld(void);
extern void GL3_MarkLeaves(void);
extern void GL3_SetupViewCluster(void);
extern void GL3_RecursiveWorldNode(entity_t* ent, mnode_t* node, const vec3_t modelorg);
extern void GL3_DrawTextureChains(entity_t* ent);
extern void GL3_DrawTextureChainsShadowPass(entity_t *currententity);

// gl3_surfbatch.c
extern void GL3_SurfBatch_Clear();
extern void GL3_SurfBatch_Begin();
extern void GL3_SurfBatch_Flush();
extern void GL3_SurfBatch_Add(msurface_t* fa);
extern void GL3_SurfBatch_DrawSingle(msurface_t* fa);
extern void GL3_SurfBatch_RenderWorldPoly(entity_t *currententity, gl3image_t* image, msurface_t *fa);

// gl3_mesh.c
extern void GL3_DrawAliasModel(entity_t *e);
extern void GL3_ResetShadowAliasModels(void);
extern void GL3_DrawAliasShadows(void);
extern void GL3_ShutdownMeshes(void);

// gl3_shaders.c

extern qboolean GL3_RecreateShaders(void);
extern qboolean GL3_InitShaders(void);
extern void GL3_ShutdownShaders(void);
extern void GL3_UpdateUBOCommon(void);
extern void GL3_UpdateUBO2D(void);
extern void GL3_UpdateUBO3D(void);
extern void GL3_UpdateUBOLights(void);
extern void GL3_UpdateUBOStyles(void);

// gl3_shadow.c

extern qboolean gl3_rendering_shadow_maps;
extern void GL3_Shadow_RenderShadowMaps();

// gl3_view.c

extern void GL3_SetViewParams(const vec3_t pos, const vec3_t angles, float fovX, float fovY, float nearClip, float farClip, float aspectRatio);

// gl3_debug.c

extern void GL3_Debug_Init();
extern void GL3_Debug_AddLine(const vec3_t p1, const vec3_t p2, const vec3_t color);
extern void GL3_Debug_AddSphere(const vec3_t origin, float radius, const vec3_t color);
extern void GL3_Debug_AddPlane(const vec3_t origin, const vec3_t normal, float size, const vec3_t color);
extern void GL3_Debug_AddBox(const vec3_t mins, const vec3_t maxs, const vec3_t color);

// Adds the current frustum to the debug renderer
extern void GL3_Debug_AddFrustum(const vec3_t color);

extern void GL3_Debug_Draw();
extern void GL3_Debug_Shutdown();

// ############ Cvars ###########

extern cvar_t *gl_msaa_samples;
extern cvar_t *r_vsync;
extern cvar_t *r_retexturing;
extern cvar_t *r_scale8bittextures;
extern cvar_t *vid_fullscreen;
extern cvar_t *r_mode;
extern cvar_t *r_customwidth;
extern cvar_t *r_customheight;

extern cvar_t *r_2D_unfiltered;
extern cvar_t *gl_nolerp_list;
extern cvar_t *gl_nobind;
extern cvar_t *r_lockpvs;
extern cvar_t *r_novis;

extern cvar_t *gl_cull;
extern cvar_t *gl_zfix;
extern cvar_t *r_fullbright;

extern cvar_t *r_norefresh;
extern cvar_t *gl_lefthand;
extern cvar_t *r_gunfov;
extern cvar_t *r_farsee;
extern cvar_t *r_drawworld;

extern cvar_t *vid_gamma;
extern cvar_t *gl3_intensity;
extern cvar_t *gl3_intensity_2D;
extern cvar_t *gl_anisotropic;
extern cvar_t *gl_texturemode;

extern cvar_t *r_lightlevel;
extern cvar_t *gl3_overbrightbits;
extern cvar_t *gl3_particle_fade_factor;
extern cvar_t *gl3_particle_square;

extern cvar_t *r_modulate;
extern cvar_t *gl_lightmap;
extern cvar_t *gl_shadows;
extern cvar_t *r_fixsurfsky;

extern cvar_t *gl3_debugcontext;

extern cvar_t *r_renderscale;

extern cvar_t *r_motionblur;
extern cvar_t* r_motionblur_samples;
extern cvar_t* r_bloom;
extern cvar_t* r_bloom_threshold;
extern cvar_t *r_ssao;
extern cvar_t *r_ssao_radius;
extern cvar_t *r_hdr;
extern cvar_t *r_hdr_exposure;
extern cvar_t *r_dithering;

extern cvar_t* r_shadowmap;
extern cvar_t* r_shadowmap_maxlights;

extern cvar_t* r_fog_state;

extern entity_t* weapon_model_entity;

extern msurface_t *gl3_alpha_surfaces;

// gl3_postfx.c

typedef enum gl3_framebuffer_flag_e
{
	GL3_FRAMEBUFFER_NONE = 0,
	GL3_FRAMEBUFFER_FILTERED = 1,
	GL3_FRAMEBUFFER_MULTISAMPLED = 2,
	GL3_FRAMEBUFFER_HDR = 4,
	GL3_FRAMEBUFFER_DEPTH = 8,
	GL3_FRAMEBUFFER_SHADOWMAP = 16,
} gl3_framebuffer_flag_t;

typedef enum gl3_framebuffer_usage
{
	GL3_FB_NOTUSED,
	GL3_FB_INUSE,
	GL3_FB_DEFERRED,
} gl3_framebuffer_usage_t;

typedef struct gl3_framebuffer_s
{
	char name[32];
	GLuint id;
	gl3_framebuffer_flag_t flags;
	GLuint width, height;
	GLuint num_color_textures;
	GLuint color_textures[4];
	GLuint depth_texture;
	gl3_framebuffer_usage_t inUse;
	int framesWithoutUse;
} gl3_framebuffer_t;

extern void GL3_CreateFramebuffer(
	GLuint width,
	GLuint height,
	GLuint num_color_textures,
	gl3_framebuffer_flag_t flags,
	gl3_framebuffer_t* out
);
extern void GL3_DestroyFramebuffer(gl3_framebuffer_t* fb);
extern void GL3_BindFramebuffer(const gl3_framebuffer_t* fb, qboolean clear);
extern void GL3_UnbindFramebuffer();
extern void GL3_BindFramebufferTexture(const gl3_framebuffer_t* fb, int index, int unit);
extern void GL3_BindFramebufferDepthTexture(const gl3_framebuffer_t* fb, int unit);

extern gl3_framebuffer_t* GL3_NewFramebuffer(
	GLuint width,
	GLuint height,
	GLuint numColorTextures,
	gl3_framebuffer_flag_t flags
);

// Borrows a framebuffer by searching in the list of available FBOs or creating a new one
extern gl3_framebuffer_t* GL3_BorrowFramebuffer(
	GLuint width,
	GLuint height,
	GLuint numColorTextures,
	gl3_framebuffer_flag_t flags,
	const char* name
);

// Return framebuffer (make available for others to use)
extern void GL3_ReturnFramebuffer(gl3_framebuffer_t* fbo);

// Return framebuffer at end of frame
extern void GL3_DeferReturnFramebuffer(gl3_framebuffer_t* fbo);

// Called at end of frame, returns all deferred FBOs
extern void GL3_FramebuffersEndFrame();

extern void GL3_DestroyAllFramebuffers();

extern void GL3_PostFx_Init();
extern void GL3_PostFx_Shutdown();
extern void GL3_PostFx_BeforeScene();
extern void GL3_PostFx_AfterScene();

// gl3_misc.c
qboolean GL3_Matrix4_Invert(const float *m, float *out);

// gl3_shadows.c
typedef enum {
	SHADOWMODE_DYNAMIC,
	SHADOWMODE_NOSHADOW,
	SHADOWMODE_STATIC,
} gl3ShadowMode_t;

typedef enum gl3ShadowLightType {
	SHADOW_LIGHT_TYPE_SUN,
	SHADOW_LIGHT_TYPE_SPOT,
	SHADOW_LIGHT_TYPE_POINT,
} gl3ShadowLightType_t;

typedef struct gl3ShadowView {
	hmm_mat4 viewMatrix;
	hmm_mat4 projMatrix;
	vec3_t angles; // (pitch yaw roll)
} gl3ShadowView_t;

typedef struct gl3ShadowLight {
	int id;
	gl3ShadowLightType_t type;
	gl3ShadowMode_t mode;
	struct gl3ShadowLight* staticOwner;
	qboolean staticMapInvalidated;
	vec3_t position;
	vec3_t angles;
	vec3_t color;
	int dlightIndex;
	float radius;
	float coneAngle;
	float bias;
	int shadowMapX;
	int shadowMapY;
	int shadowMapWidth;
	int shadowMapHeight;
	size_t numShadowViews;
	gl3ShadowView_t shadowViews[6];
} gl3ShadowLight_t;

void GL3_Shadow_Init();
void GL3_Shadow_BeginFrame();

// Creates a light that has baked shadows.
extern int GL3_Shadow_CreateStaticLight(const vec3_t pos, float radius, gl3ShadowMode_t mode);

// Renders the static portion of the light if needed.
// Returns true if we could add this light to the frame's shadow map or false otherwise.
extern qboolean GL3_Shadow_TouchStaticLight(int dlightIndex, int staticLightIndex);

// Returns true if we could add this light to the frame's shadow map or false otherwise.
extern qboolean GL3_Shadow_AddDynLight(int dlightIndex, const vec3_t pos, float radius, qboolean isMapLight, gl3ShadowMode_t mode);

extern void GL3_Shadow_RenderShadowMaps();
extern void GL3_Shadow_Shutdown();

extern cvar_t* r_flashlight;

void GL3_BindShadowmap(int shadowmap);

#endif /* SRC_CLIENT_REFRESH_GL3_HEADER_LOCAL_H_ */
