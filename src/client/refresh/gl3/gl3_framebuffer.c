#include "header/local.h"

#define OPENGL_DEPTH_COMPONENT_TYPE GL_DEPTH_COMPONENT32F

static GLuint CreateFramebufferTexture(GLuint target, GLuint width, GLuint height, GLint filter, GLuint format, qboolean shadowmap)
{
	GLuint id = 0;
	glGenTextures(1, &id);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(target, id);

	glTexParameteri(target, GL_TEXTURE_MAG_FILTER, filter);
	glTexParameteri(target, GL_TEXTURE_MIN_FILTER, filter);
	glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	if (shadowmap) {
		glTexParameteri(target, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
	}

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
	GLuint format = (flags & GL3_FRAMEBUFFER_HDR) ? GL_RGBA16F : GL_RGBA8;

	if (gl_msaa_samples->value == 0.0f)
	{
		target = GL_TEXTURE_2D;
	}

	for (GLuint i = 0; i < num_color_textures; i++) {
		out->color_textures[i] = CreateFramebufferTexture(target, width, height, filter, format, false);
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
		out->depth_texture = CreateFramebufferTexture(target, width, height, filter, OPENGL_DEPTH_COMPONENT_TYPE, (flags & GL3_FRAMEBUFFER_SHADOWMAP));
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

void GL3_UnbindFramebuffer()
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, gl3_newrefdef.width, gl3_newrefdef.height);
}

void GL3_BindFramebuffer(const gl3_framebuffer_t* fb)
{
	if (!fb)
	{
		GL3_UnbindFramebuffer();
		return;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, fb->id);
	glViewport(0, 0, fb->width, fb->height);

	GLbitfield mask = GL_COLOR_BUFFER_BIT;
	if (fb->flags & GL3_FRAMEBUFFER_DEPTH) {
		mask |= GL_DEPTH_BUFFER_BIT;
	}
	glClear(mask);
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