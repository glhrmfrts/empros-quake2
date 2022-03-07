#include "header/local.h"

#define OPENGL_DEPTH_COMPONENT_TYPE GL_DEPTH_COMPONENT32F

static GLuint CreateFramebufferTexture(GLuint target, GLuint width, GLuint height, GLint filter, GLuint format)
{
    GLuint id = 0;
    glGenTextures(1, &id);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(target, id);

    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (target == GL_TEXTURE_2D_MULTISAMPLE) {
        glTexImage2DMultisample(target, (int)gl_msaa_samples->value, format, width, height, GL_FALSE);
    }
    else {
        glTexImage2D(target, 0, format, width, height, 0, (format == OPENGL_DEPTH_COMPONENT_TYPE) ? GL_DEPTH_COMPONENT : GL_BGRA, GL_UNSIGNED_BYTE, NULL);
    }

    return id;
}

void GL3_CreateFramebuffer(GLuint width, GLuint height, GLuint num_color_textures, gl3_framebuffer_flag_t flags, gl3_framebuffer_t* out)
{
    memset(out, 0, sizeof(gl3_framebuffer_t));

    out->width = width;
    out->height = height;
    out->num_color_textures = num_color_textures;
    out->flags = flags;

    glGenFramebuffers(1, &out->id);
    glBindFramebuffer(GL_FRAMEBUFFER, out->id);

    GLuint target = (flags & GL3_FRAMEBUFFER_MULTISAMPLED) ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
    GLint filter = (flags & GL3_FRAMEBUFFER_FILTERED) ? GL_LINEAR : GL_NEAREST;
    GLuint format = (flags & GL3_FRAMEBUFFER_FLOAT) ? GL_RGBA16F : GL_RGBA8;

    for (GLuint i = 0; i < num_color_textures; i++) {
        out->color_textures[i] = CreateFramebufferTexture(target, width, height, filter, format);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, target, out->color_textures[i], 0);
    }

    static GLenum framebufferColorAttachments[] = {
		GL_COLOR_ATTACHMENT0,
		GL_COLOR_ATTACHMENT1,
		GL_COLOR_ATTACHMENT2,
		GL_COLOR_ATTACHMENT3,
		GL_COLOR_ATTACHMENT4,
		GL_COLOR_ATTACHMENT5,
		GL_COLOR_ATTACHMENT6,
		GL_COLOR_ATTACHMENT7,
		GL_COLOR_ATTACHMENT8,
		GL_COLOR_ATTACHMENT9,
		GL_COLOR_ATTACHMENT10,
		GL_COLOR_ATTACHMENT11,
		GL_COLOR_ATTACHMENT12,
		GL_COLOR_ATTACHMENT13,
		GL_COLOR_ATTACHMENT14,
		GL_COLOR_ATTACHMENT15,
	};

    glDrawBuffers(num_color_textures, framebufferColorAttachments);

    if (flags & GL3_FRAMEBUFFER_DEPTH) {
        out->depth_texture = CreateFramebufferTexture(target, width, height, filter, OPENGL_DEPTH_COMPONENT_TYPE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, target, out->depth_texture, 0);
    }

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        printf("%x\n", status);
        assert(!(status == GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT));
        assert(status == GL_FRAMEBUFFER_COMPLETE);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GL3_DestroyFramebuffer(gl3_framebuffer_t* fb)
{
    glDeleteFramebuffers(1, &fb->id);
    glDeleteTextures(fb->num_color_textures, fb->color_textures);
    if (fb->depth_texture) {
        glDeleteTextures(1, &fb->depth_texture);
    }
}

void GL3_BindFramebuffer(const gl3_framebuffer_t* fb)
{
    glBindFramebuffer(GL_FRAMEBUFFER, fb->id);
    glViewport(0, 0, fb->width, fb->height);

    GLbitfield mask = GL_COLOR_BUFFER_BIT;
    if (fb->flags & GL3_FRAMEBUFFER_DEPTH) {
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    glClear(mask);
}

void GL3_UnbindFramebuffer()
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, gl3_newrefdef.width, gl3_newrefdef.height);
}

void GL3_BindFramebufferTexture(const gl3_framebuffer_t* fb, int index, int unit)
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(
        fb->flags & GL3_FRAMEBUFFER_MULTISAMPLED ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D,
        fb->color_textures[index]
    );
}

void GL3_BindFramebufferDepthTexture(const gl3_framebuffer_t* fb, int unit)
{
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(
        fb->flags & GL3_FRAMEBUFFER_MULTISAMPLED ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D,
        fb->depth_texture
    );
}

enum { MAX_COLOR_TEXTURES = 4 };

static GLuint screen_vao;
static GLuint screen_vbo;

typedef struct {
    GLint u_SampleCount;
    GLint u_Intensity;
    GLint u_FboSampler[MAX_COLOR_TEXTURES];
    GLint u_DepthSampler0;
    GLint u_ViewProjectionInverseMatrix;
    GLint u_PreviousViewProjectionMatrix;
} uniforms_t;

static uniforms_t resolve_multisample_uniforms;
static uniforms_t motion_blur_uniforms;

static gl3_framebuffer_t resolve_multisample_fbo;
static gl3_framebuffer_t motion_blur_mask_fbo;
static gl3_framebuffer_t motion_blur_fbo;

cvar_t* r_motionblur;
entity_t* weapon_model_entity;

static GLint GetUniform(const gl3ShaderInfo_t* si, const char* name, const char* shader)
{
    int u = glGetUniformLocation(si->shaderProgram, name);
    if (u == -1) {
        R_Printf(PRINT_ALL, "WARNING: Failed to find uniform %s in shader %s\n", name, shader);
    }
    return u;
}

static void GetUniforms(const gl3ShaderInfo_t* si, const char* shadername, uniforms_t* uniforms)
{
    uniforms->u_SampleCount = GetUniform(si, "u_SampleCount", shadername);
    uniforms->u_Intensity = GetUniform(si, "u_Intensity", shadername);

    for (int i = 0; i < MAX_COLOR_TEXTURES; i++) {
        char uname[] = "u_FboSampler#";
        uname[strlen(uname) - 1] = '0' + i;
        uniforms->u_FboSampler[i] = GetUniform(si, uname, shadername);
    }

    uniforms->u_DepthSampler0 = GetUniform(si, "u_DepthSampler0", shadername);
    uniforms->u_ViewProjectionInverseMatrix = GetUniform(si, "u_ViewProjectionInverseMatrix", shadername);
    uniforms->u_PreviousViewProjectionMatrix = GetUniform(si, "u_PreviousViewProjectionMatrix", shadername);
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
    GL3_CreateFramebuffer(width, height, 2, GL3_FRAMEBUFFER_MULTISAMPLED | GL3_FRAMEBUFFER_DEPTH, &resolve_multisample_fbo);
    GL3_CreateFramebuffer(width, height, 1, GL3_FRAMEBUFFER_DEPTH, &motion_blur_mask_fbo);
    GL3_CreateFramebuffer(width, height, 2, GL3_FRAMEBUFFER_NONE, &motion_blur_fbo);

    GetUniforms(&gl3state.siPostfxResolveMultisample, "ResolveMultisample", &resolve_multisample_uniforms);
    GetUniforms(&gl3state.siPostfxMotionBlur, "MotionBlur", &motion_blur_uniforms);
}

void GL3_PostFx_Shutdown()
{
    GL3_DestroyFramebuffer(&resolve_multisample_fbo);
    glDeleteVertexArrays(1, &screen_vao);
    glDeleteBuffers(1, &screen_vbo);
    gl3state.postfx_initialized = false;
}

#define POSTFX 1

void GL3_PostFx_BeforeScene()
{
#if POSTFX
    if (!gl3state.postfx_initialized) {
        gl3state.postfx_initialized = true;
        GL3_PostFx_Init();
    }
    GL3_BindFramebuffer(&resolve_multisample_fbo);
    weapon_model_entity = NULL;
#endif
}

void GL3_PostFx_AfterScene()
{
#if POSTFX
    static hmm_mat4 previousviewproj;
    hmm_mat4 viewproj = HMM_MultiplyMat4(gl3state.uni3DData.transProjMat4, gl3state.uni3DData.transViewMat4);
    hmm_mat4 viewprojinv;
    GL3_Matrix4_Invert((const float*)viewproj.Elements, (float*)viewprojinv.Elements);

// resolve multisample -- 2 outputs (color + depth)
    if (r_motionblur->value > 0.0f)
    {
        GL3_BindFramebuffer(&motion_blur_fbo);
    }
    else
    {
        GL3_UnbindFramebuffer();
    }

    GL3_UseProgram(gl3state.siPostfxResolveMultisample.shaderProgram);
    glUniform1i(resolve_multisample_uniforms.u_FboSampler[0], 0);
    glUniform1i(resolve_multisample_uniforms.u_DepthSampler0, 1);
    glUniform1i(resolve_multisample_uniforms.u_SampleCount, gl_msaa_samples->value);
    GL3_BindFramebufferTexture(&resolve_multisample_fbo, 0, 0);
    GL3_BindFramebufferDepthTexture(&resolve_multisample_fbo, 1);
    GL3_BindVAO(screen_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);

// motion blur mask
    if (r_motionblur->value > 0.0f && weapon_model_entity)
    {
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        GL3_BindFramebuffer(&motion_blur_mask_fbo);
        GL3_DrawAliasModel(weapon_model_entity);
        glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
    }

// motion blur -- 2 outputs (color + depth)
    if (r_motionblur->value > 0.0f)
    {
        GL3_UnbindFramebuffer();

        GL3_UseProgram(gl3state.siPostfxMotionBlur.shaderProgram);
        glUniform1i(motion_blur_uniforms.u_FboSampler[0], 0);
        glUniform1i(motion_blur_uniforms.u_FboSampler[1], 1);
        glUniform1i(motion_blur_uniforms.u_FboSampler[2], 2);
        glUniform1f(motion_blur_uniforms.u_Intensity, r_motionblur->value);
        glUniformMatrix4fv(motion_blur_uniforms.u_ViewProjectionInverseMatrix, 1, false, (const GLfloat*)viewprojinv.Elements);
        glUniformMatrix4fv(motion_blur_uniforms.u_PreviousViewProjectionMatrix, 1, false, (const GLfloat*)previousviewproj.Elements);
        GL3_BindFramebufferTexture(&motion_blur_fbo, 0, 0);
        GL3_BindFramebufferTexture(&motion_blur_fbo, 1, 1);
        GL3_BindFramebufferTexture(&motion_blur_mask_fbo, 0, 2);
        GL3_BindVAO(screen_vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    previousviewproj = viewproj;

    GL3_InvalidateTextureBindings();
#endif
}