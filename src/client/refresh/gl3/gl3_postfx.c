#include "header/local.h"

enum { MAX_COLOR_TEXTURES = 4 };

static GLuint screen_vao;
static GLuint screen_vbo;

typedef struct {
	GLint u_SampleCount;
	GLint u_Intensity;
	GLint u_HDR;
	GLint u_HDRMode;
	GLint u_Size;
	GLint u_AspectRatio;
	GLint u_TanHalfFOV;
	GLint u_FboSampler[MAX_COLOR_TEXTURES];
	GLint u_DepthSampler0;
	GLint u_ProjectionMatrix;
	GLint u_ViewProjectionMatrix;
	GLint u_ViewProjectionInverseMatrix;
	GLint u_PreviousViewProjectionMatrix;
} uniforms_t;

static uniforms_t resolve_multisample_uniforms;
static uniforms_t resolve_hdr_uniforms;
static uniforms_t bloom_filter_uniforms;
static uniforms_t bloom_blur_uniforms;
static uniforms_t ssao_map_uniforms;
static uniforms_t ssao_blur_uniforms;
static uniforms_t motion_blur_uniforms;
static uniforms_t blit_uniforms;

static gl3_framebuffer_t scene_fbo;
static gl3_framebuffer_t resolve_multisample_fbo;
static gl3_framebuffer_t resolve_hdr_fbo;
static gl3_framebuffer_t bloom_filter_fbo;
static gl3_framebuffer_t bloom_blur_fbo[2];
static gl3_framebuffer_t ssao_geom_fbo;
static gl3_framebuffer_t ssao_map_fbo;
static gl3_framebuffer_t ssao_blur_fbo;
static gl3_framebuffer_t motion_blur_mask_fbo;

enum { SSAO_KERNEL_SIZE = 64 };

static GLuint ssao_noise_texture;
static GLuint ssao_kernel_ubo;
static hmm_vec4 ssao_kernel[SSAO_KERNEL_SIZE];

cvar_t* r_motionblur;
cvar_t* r_motionblur_samples;
cvar_t* r_hdr;
cvar_t* r_hdr_exposure;
cvar_t* r_bloom;
cvar_t* r_bloom_threshold;
cvar_t* r_ssao;
entity_t* weapon_model_entity;

static GLint GetUniform(const gl3ShaderInfo_t* si, const char* name, const char* shader)
{
	int u = glGetUniformLocation(si->shaderProgram, name);
	if (u == -1) {
		//R_Printf(PRINT_ALL, "WARNING: Failed to find uniform %s in shader %s\n", name, shader);
	}
	return u;
}

static void GetUniforms(const gl3ShaderInfo_t* si, const char* shadername, uniforms_t* uniforms)
{
	uniforms->u_SampleCount = GetUniform(si, "u_SampleCount", shadername);
	uniforms->u_Intensity = GetUniform(si, "u_Intensity", shadername);
	uniforms->u_HDR = GetUniform(si, "u_HDR", shadername);
	uniforms->u_HDRMode = GetUniform(si, "u_HDRMode", shadername);
	uniforms->u_AspectRatio = GetUniform(si, "u_AspectRatio", shadername);
	uniforms->u_TanHalfFOV = GetUniform(si, "u_TanHalfFOV", shadername);
	uniforms->u_ProjectionMatrix = GetUniform(si, "u_ProjectionMatrix", shadername);
	uniforms->u_ViewProjectionMatrix = GetUniform(si, "u_ViewProjectionMatrix", shadername);
	uniforms->u_ViewProjectionInverseMatrix = GetUniform(si, "u_ViewProjectionInverseMatrix", shadername);
	uniforms->u_PreviousViewProjectionMatrix = GetUniform(si, "u_PreviousViewProjectionMatrix", shadername);
	uniforms->u_DepthSampler0 = GetUniform(si, "u_DepthSampler0", shadername);
	uniforms->u_Size = GetUniform(si, "u_Size", shadername);
	for (int i = 0; i < MAX_COLOR_TEXTURES; i++) {
		char uname[] = "u_FboSampler#";
		uname[strlen(uname) - 1] = '0' + i;
		uniforms->u_FboSampler[i] = GetUniform(si, uname, shadername);
	}
}

static void GL3_BindTexture(int unit, GLuint texnum)
{
	glActiveTexture(GL_TEXTURE0 + unit);
	glBindTexture(GL_TEXTURE_2D, texnum);
}

static void GL3_BindTextureMultisampled(int unit, GLuint texnum)
{
	glActiveTexture(GL_TEXTURE0 + unit);
	glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, texnum);
}

void GL3_PostFx_Init()
{
	glGenVertexArrays(1, &screen_vao);
	glGenBuffers(1, &screen_vbo);

	GL3_BindVAO(screen_vao);
	GL3_BindVBO(screen_vbo);

	glEnableVertexAttribArray(GL3_ATTRIB_POSITION);
	qglVertexAttribPointer(GL3_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), 0);

	glEnableVertexAttribArray(GL3_ATTRIB_TEXCOORD);
	qglVertexAttribPointer(GL3_ATTRIB_TEXCOORD, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), 3 * sizeof(float));

	static const float vertices[] = {
		-1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
		-1.0f, 1.0f, 0.0f,  0.0f, 1.0f,
		1.0f, 1.0f, 0.0f,  1.0f, 1.0f,

		-1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
		1.0f, 1.0f, 0.0f,  1.0f, 1.0f,
		1.0f, -1.0f, 0.0f,  1.0f, 0.0f,
	};
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), (const void*)vertices, GL_STATIC_DRAW);

	GLuint width = gl3_newrefdef.width;
	GLuint height = gl3_newrefdef.height;
	GL3_CreateFramebuffer(width, height, 2,
		GL3_FRAMEBUFFER_MULTISAMPLED | GL3_FRAMEBUFFER_DEPTH | GL3_FRAMEBUFFER_HDR,
		&scene_fbo);
	GL3_CreateFramebuffer(width, height, 2, GL3_FRAMEBUFFER_HDR, &resolve_multisample_fbo);

	GL3_CreateFramebuffer(width/2, height/2, 1, GL3_FRAMEBUFFER_FILTERED | GL3_FRAMEBUFFER_HDR, &bloom_filter_fbo);
	GL3_CreateFramebuffer(width/2, height/2, 1, GL3_FRAMEBUFFER_FILTERED | GL3_FRAMEBUFFER_HDR, &bloom_blur_fbo[0]);
	GL3_CreateFramebuffer(width/2, height/2, 1, GL3_FRAMEBUFFER_FILTERED | GL3_FRAMEBUFFER_HDR, &bloom_blur_fbo[1]);

	GL3_CreateFramebuffer(width, height, 1, GL3_FRAMEBUFFER_NONE, &resolve_hdr_fbo);

	GL3_CreateFramebuffer(width, height, 2, GL3_FRAMEBUFFER_HDR | GL3_FRAMEBUFFER_DEPTH, &ssao_geom_fbo);
	GL3_CreateFramebuffer(width, height, 1, GL3_FRAMEBUFFER_FILTERED, &ssao_map_fbo);
	GL3_CreateFramebuffer(width/2, height/2, 1, GL3_FRAMEBUFFER_FILTERED, &ssao_blur_fbo);

	GL3_CreateFramebuffer(width, height, 1, GL3_FRAMEBUFFER_DEPTH, &motion_blur_mask_fbo);

	GetUniforms(&gl3state.siPostfxResolveMultisample, "ResolveMultisample", &resolve_multisample_uniforms);
	GetUniforms(&gl3state.siPostfxResolveHDR, "ResolveHDR", &resolve_hdr_uniforms);
	GetUniforms(&gl3state.siPostfxBloomFilter, "BloomFilter", &bloom_filter_uniforms);
	GetUniforms(&gl3state.siPostfxBloomBlur, "BloomBlur", &bloom_blur_uniforms);
	GetUniforms(&gl3state.siPostfxSSAO, "SSAO", &ssao_map_uniforms);
	GetUniforms(&gl3state.siPostfxSSAOBlur, "SSAOBlur", &ssao_blur_uniforms);
	GetUniforms(&gl3state.siPostfxMotionBlur, "MotionBlur", &motion_blur_uniforms);
	GetUniforms(&gl3state.siPostfxBlit, "Blit", &blit_uniforms);

	for (int i = 0; i < SSAO_KERNEL_SIZE; i++)
	{
		ssao_kernel[i] = HMM_Vec4(
			crandk(), // between -1 and 1
			crandk(), // between -1 and 1
			frandk(), // between 0 and 1
			0.0f
		);
	}

	GLuint blockIndex = glGetUniformBlockIndex(gl3state.siPostfxSSAO.shaderProgram, "uniSSAO");
	glUniformBlockBinding(gl3state.siPostfxSSAO.shaderProgram, blockIndex, GL3_BINDINGPOINT_UNISSAO);

	glGenBuffers(1, &ssao_kernel_ubo);
	glBindBuffer(GL_UNIFORM_BUFFER, ssao_kernel_ubo);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(ssao_kernel), (const void*)ssao_kernel, GL_STATIC_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, GL3_BINDINGPOINT_UNISSAO, ssao_kernel_ubo);
	gl3state.currentUBO = ssao_kernel_ubo;

	hmm_vec3 ssao_noise[16];
	for (unsigned int i = 0; i < 16; i++)
	{
		ssao_noise[i] = HMM_Vec3(
			crandk(),
			crandk(),
			0.0f
		);
	}

	glGenTextures(1, &ssao_noise_texture);
	glBindTexture(GL_TEXTURE_2D, ssao_noise_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 4, 4, 0, GL_RGB, GL_FLOAT, &ssao_noise[0]);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

void GL3_PostFx_Shutdown()
{
	GL3_DestroyFramebuffer(&resolve_multisample_fbo);
	glDeleteVertexArrays(1, &screen_vao);
	glDeleteBuffers(1, &screen_vbo);
	gl3state.postfx_initialized = false;
}

#define POSTFX 1

static hmm_mat4 previousviewproj;
static hmm_mat4 viewproj;
static hmm_mat4 viewprojinv;

void GL3_PostFx_BeforeScene()
{
#if POSTFX
	if (!gl3state.postfx_initialized)
	{
		gl3state.postfx_initialized = true;
		GL3_PostFx_Init();
	}

	viewproj = HMM_MultiplyMat4(gl3state.uni3DData.transProjMat4, gl3state.uni3DData.transViewMat4);
	GL3_Matrix4_Invert((const float*)viewproj.Elements, (float*)viewprojinv.Elements);

	if (r_ssao->value)
	{
		gl3state.render_pass = RENDER_PASS_SSAO;

		GL3_BindFramebuffer(&ssao_geom_fbo);
		GL3_UseProgram(gl3state.si3DSSAO.shaderProgram);

		// render the opaque world geometry
		entity_t ent = {0};
		ent.frame = (int)(gl3_newrefdef.time * 2);
		GL3_RecursiveWorldNode(&ent, gl3_worldmodel->nodes, gl3_newrefdef.vieworg);
		GL3_DrawTextureChains(&ent);
		GL3_DrawEntitiesOnList();

		hmm_vec2 noise_scale;
		noise_scale.X = (float)gl3_newrefdef.width / 4.0f;
		noise_scale.Y = (float)gl3_newrefdef.height / 4.0f;

		GL3_BindFramebuffer(&ssao_map_fbo);
		GL3_UseProgram(gl3state.siPostfxSSAO.shaderProgram);
		glUniform1i(ssao_map_uniforms.u_FboSampler[0], 0);
		glUniform1i(ssao_map_uniforms.u_FboSampler[1], 1);
		glUniform1i(ssao_map_uniforms.u_FboSampler[2], 2);
		glUniform1f(ssao_map_uniforms.u_Intensity, 1.0f);
		glUniform2f(ssao_map_uniforms.u_Size, noise_scale.X, noise_scale.Y);
		glUniform1f(ssao_map_uniforms.u_AspectRatio, (float)gl3_newrefdef.width / (float)gl3_newrefdef.height);
		glUniform1f(ssao_map_uniforms.u_TanHalfFOV, HMM_TanF(HMM_ToRadians(gl3_newrefdef.fov_x/2.0f)));
		glUniformMatrix4fv(ssao_map_uniforms.u_ProjectionMatrix, 1, false, (const GLfloat*)gl3state.uni3DData.transProjMat4.Elements);
		GL3_BindFramebufferTexture(&ssao_geom_fbo, 0, 0);
		GL3_BindFramebufferTexture(&ssao_geom_fbo, 1, 1);
		glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, ssao_noise_texture);
		GL3_BindVAO(screen_vao);
		glDrawArrays(GL_TRIANGLES, 0, 6);

		GL3_BindFramebuffer(&ssao_blur_fbo);
		GL3_UseProgram(gl3state.siPostfxSSAOBlur.shaderProgram);
		glUniform1i(ssao_blur_uniforms.u_FboSampler[0], 0);
		//glUniform1i(ssao_blur_uniforms.u_FboSampler[1], 1);
		glUniform2f(ssao_blur_uniforms.u_Size, ssao_blur_fbo.width, ssao_blur_fbo.height);
		GL3_BindFramebufferTexture(&ssao_map_fbo, 0, 0);
		//glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, ssao_noise_texture);
		GL3_BindVAO(screen_vao);
		glDrawArrays(GL_TRIANGLES, 0, 6);

		GL3_BindFramebufferTexture(&ssao_blur_fbo, 0, GL3_SSAO_MAP_TU - GL_TEXTURE0);

		GL3_InvalidateTextureBindings();
	}

	gl3state.render_pass = RENDER_PASS_SCENE;
	gl3state.uni3DData.ssao = r_ssao->value;

	GL3_BindFramebuffer(&scene_fbo);
	weapon_model_entity = NULL;
#endif
}

static gl3_framebuffer_t* RenderResolveMultisample(GLuint scene_texture, GLuint scene_depth_texture)
{
	GL3_BindFramebuffer(&resolve_multisample_fbo);
	GL3_UseProgram(gl3state.siPostfxResolveMultisample.shaderProgram);
	glUniform1i(resolve_multisample_uniforms.u_FboSampler[0], 0);
	glUniform1i(resolve_multisample_uniforms.u_DepthSampler0, 1);
	glUniform1i(resolve_multisample_uniforms.u_SampleCount, gl_msaa_samples->value);
	glUniform1f(resolve_multisample_uniforms.u_HDR, r_hdr->value);
	glUniform1f(resolve_multisample_uniforms.u_Intensity, r_hdr_exposure->value);
	GL3_BindTextureMultisampled(0, scene_texture);
	GL3_BindTextureMultisampled(1, scene_depth_texture);
	GL3_BindVAO(screen_vao);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	return &resolve_multisample_fbo;
}

static gl3_framebuffer_t* RenderBloom(GLuint scene_texture)
{
// bloom filter
	GL3_BindFramebuffer(&bloom_filter_fbo);
	GL3_UseProgram(gl3state.siPostfxBloomFilter.shaderProgram);
	glUniform1i(bloom_filter_uniforms.u_FboSampler[0], 0);
	glUniform1f(bloom_filter_uniforms.u_Intensity, r_bloom_threshold->value);
	GL3_BindTexture(0, scene_texture);
	GL3_BindVAO(screen_vao);
	glDrawArrays(GL_TRIANGLES, 0, 6);

// bloom blurs
	gl3_framebuffer_t* bloom_last_fbo;
	{
		qboolean horizontal = true;
		qboolean first_iteration = true;
		int amount = 10;
		GL3_UseProgram(gl3state.siPostfxBloomBlur.shaderProgram);
		for (unsigned int i = 0; i < amount; i++)
		{
			bloom_last_fbo = &bloom_blur_fbo[horizontal];
			GL3_BindFramebuffer(bloom_last_fbo);
			glUniform1i(bloom_blur_uniforms.u_SampleCount, horizontal);
			GL3_BindFramebufferTexture(
				first_iteration ? (&bloom_filter_fbo) : (&bloom_blur_fbo[!horizontal]),
				0, 0
			);
			GL3_BindVAO(screen_vao);
			glDrawArrays(GL_TRIANGLES, 0, 6);
			horizontal = !horizontal;
			if (first_iteration)
				first_iteration = false;
		}
	}
	return bloom_last_fbo;
}

static gl3_framebuffer_t* RenderResolveHDR(GLuint scene_texture, GLuint bloom_texture)
{
	GL3_BindFramebuffer(&resolve_hdr_fbo);
	GL3_UseProgram(gl3state.siPostfxResolveHDR.shaderProgram);
	glUniform1i(resolve_hdr_uniforms.u_FboSampler[0], 0);
	glUniform1i(resolve_hdr_uniforms.u_FboSampler[1], 1);
	glUniform1f(resolve_hdr_uniforms.u_Intensity, r_bloom->value);
	glUniform1f(resolve_hdr_uniforms.u_HDR, r_hdr_exposure->value);
	GL3_BindTexture(0, scene_texture);
	GL3_BindTexture(1, bloom_texture);
	GL3_BindVAO(screen_vao);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	return &resolve_hdr_fbo;
}

static gl3_framebuffer_t* RenderMotionBlurMask(entity_t* weapon_model_entity)
{
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	GL3_BindFramebuffer(&motion_blur_mask_fbo);
	GL3_DrawAliasModel(weapon_model_entity);
	glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
	return &motion_blur_mask_fbo;
}

static gl3_framebuffer_t* RenderMotionBlur(
	GLuint scene_texture, GLuint depth_texture, GLuint mask_texture
)
{
	GL3_UnbindFramebuffer();
	GL3_UseProgram(gl3state.siPostfxMotionBlur.shaderProgram);
	glUniform1i(motion_blur_uniforms.u_FboSampler[0], 0);
	glUniform1i(motion_blur_uniforms.u_FboSampler[1], 1);
	glUniform1i(motion_blur_uniforms.u_FboSampler[2], 2);
	glUniform1f(motion_blur_uniforms.u_Intensity, r_motionblur->value * 0.2f);
	glUniform1i(motion_blur_uniforms.u_SampleCount, (int)r_motionblur_samples->value);
	glUniformMatrix4fv(motion_blur_uniforms.u_ViewProjectionInverseMatrix, 1, false, (const GLfloat*)viewprojinv.Elements);
	glUniformMatrix4fv(motion_blur_uniforms.u_PreviousViewProjectionMatrix, 1, false, (const GLfloat*)previousviewproj.Elements);
	GL3_BindTexture(0, scene_texture);
	GL3_BindTexture(1, depth_texture);
	GL3_BindTexture(2, mask_texture);
	GL3_BindVAO(screen_vao);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	return NULL;
}

static void RenderBlit(GLuint tex)
{
	GL3_UnbindFramebuffer();
	GL3_UseProgram(gl3state.siPostfxBlit.shaderProgram);
	glUniform1i(blit_uniforms.u_FboSampler[0], 0);
	GL3_BindTexture(0, tex);
	GL3_BindVAO(screen_vao);
	glDrawArrays(GL_TRIANGLES, 0, 6);
}

void GL3_PostFx_AfterScene()
{
#if POSTFX
// resolve multisample -- 2 outputs (color + depth)
	gl3_framebuffer_t* output;
	GLuint scene_texture, depth_texture;

	output = &scene_fbo;
	scene_texture = output->color_textures[0];
	depth_texture = output->depth_texture;

	qboolean multisampled = gl_msaa_samples->value > 0.0f;
	if (multisampled)
	{
		output = RenderResolveMultisample(scene_texture, depth_texture);
		scene_texture = output->color_textures[0];
		depth_texture = output->color_textures[1];
	}

	if (r_hdr->value)
	{
		GLuint bloom_texture = scene_texture;
		if (r_bloom->value)
		{
			output = RenderBloom(scene_texture);
			bloom_texture = output->color_textures[0];
		}
		output = RenderResolveHDR(scene_texture, bloom_texture);
		scene_texture = output->color_textures[0];
	}

	if (r_motionblur->value)
	{
		if (weapon_model_entity)
		{
			RenderMotionBlurMask(weapon_model_entity);
		}
		output = RenderMotionBlur(scene_texture, depth_texture, motion_blur_mask_fbo.color_textures[0]);
	}
	else
	{
		RenderBlit(scene_texture);
	}

	previousviewproj = viewproj;

	GL3_InvalidateTextureBindings();
#endif
}
