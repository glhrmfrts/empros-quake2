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
 * OpenGL3 refresher: Handling shaders
 *
 * =======================================================================
 */

// TODO: load shaders from external files for better dev experience

#include "header/local.h"

// TODO: remove eprintf() usage
#define eprintf(...)  R_Printf(PRINT_ALL, __VA_ARGS__)

#define MULTILINE_STRING(...) #__VA_ARGS__

#include "shaders/alias.h"
#include "shaders/color3d.h"
#include "shaders/common2d.h"
#include "shaders/common3d.h"
#include "shaders/debug.h"
#include "shaders/flow.h"
#include "shaders/particles.h"
#include "shaders/postfx.h"
#include "shaders/shadowmap.h"
#include "shaders/sky.h"
#include "shaders/sprite.h"
#include "shaders/ssao.h"
#include "shaders/texture3d.h"
#include "shaders/water.h"
#include "shaders/world.h"

static GLuint
CompileShader(GLenum shaderType, const char* shaderSrc, const char* shaderSrc2)
{
	GLuint shader = glCreateShader(shaderType);

	const char* sources[2] = { shaderSrc, shaderSrc2 };
	int numSources = shaderSrc2 != NULL ? 2 : 1;

	glShaderSource(shader, numSources, sources, NULL);
	glCompileShader(shader);
	GLint status;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if(status != GL_TRUE)
	{
		char buf[2048];
		char* bufPtr = buf;
		int bufLen = sizeof(buf);
		GLint infoLogLength;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLength);
		if(infoLogLength >= bufLen)
		{
			bufPtr = malloc(infoLogLength+1);
			bufLen = infoLogLength+1;
			if(bufPtr == NULL)
			{
				bufPtr = buf;
				bufLen = sizeof(buf);
				eprintf("WARN: In CompileShader(), malloc(%d) failed!\n", infoLogLength+1);
			}
		}

		glGetShaderInfoLog(shader, bufLen, NULL, bufPtr);

		const char* shaderTypeStr = "";
		switch(shaderType)
		{
			case GL_VERTEX_SHADER:   shaderTypeStr = "Vertex"; break;
			case GL_FRAGMENT_SHADER: shaderTypeStr = "Fragment"; break;
			case GL_GEOMETRY_SHADER: shaderTypeStr = "Geometry"; break;
			/* not supported in OpenGL3.2 and we're unlikely to need/use them anyway
			case GL_COMPUTE_SHADER:  shaderTypeStr = "Compute"; break;
			case GL_TESS_CONTROL_SHADER:    shaderTypeStr = "TessControl"; break;
			case GL_TESS_EVALUATION_SHADER: shaderTypeStr = "TessEvaluation"; break;
			*/
		}
		eprintf("ERROR: Compiling %s Shader failed: %s\n", shaderTypeStr, bufPtr);
		glDeleteShader(shader);

		if(bufPtr != buf)  free(bufPtr);

		return 0;
	}

	return shader;
}

static GLuint
CreateShaderProgram(int numShaders, const GLuint* shaders)
{
	int i=0;
	GLuint shaderProgram = glCreateProgram();
	if(shaderProgram == 0)
	{
		eprintf("ERROR: Couldn't create a new Shader Program!\n");
		return 0;
	}

	for(i=0; i<numShaders; ++i)
	{
		glAttachShader(shaderProgram, shaders[i]);
	}

	// make sure all shaders use the same attribute locations for common attributes
	// (so the same VAO can easily be used with different shaders)
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_POSITION, "position");
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_TEXCOORD, "texCoord");
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_LMTEXCOORD, "lmTexCoord");
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_COLOR, "vertColor");
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_NORMAL, "normal");
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_LIGHTFLAGS, "lightFlags");
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_STYLE0, "style0");
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_STYLE1, "style1");
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_STYLE2, "style2");
	glBindAttribLocation(shaderProgram, GL3_ATTRIB_STYLE3, "style3");

	// the following line is not necessary/implicit (as there's only one output)
	// glBindFragDataLocation(shaderProgram, 0, "outColor"); XXX would this even be here?

	glLinkProgram(shaderProgram);

	GLint status;
	glGetProgramiv(shaderProgram, GL_LINK_STATUS, &status);
	if(status != GL_TRUE)
	{
		char buf[2048];
		char* bufPtr = buf;
		int bufLen = sizeof(buf);
		GLint infoLogLength;
		glGetProgramiv(shaderProgram, GL_INFO_LOG_LENGTH, &infoLogLength);
		if(infoLogLength >= bufLen)
		{
			bufPtr = malloc(infoLogLength+1);
			bufLen = infoLogLength+1;
			if(bufPtr == NULL)
			{
				bufPtr = buf;
				bufLen = sizeof(buf);
				eprintf("WARN: In CreateShaderProgram(), malloc(%d) failed!\n", infoLogLength+1);
			}
		}

		glGetProgramInfoLog(shaderProgram, bufLen, NULL, bufPtr);

		eprintf("ERROR: Linking shader program failed: %s\n", bufPtr);

		glDeleteProgram(shaderProgram);

		if(bufPtr != buf)  free(bufPtr);

		return 0;
	}

	for(i=0; i<numShaders; ++i)
	{
		// after linking, they don't need to be attached anymore.
		// no idea  why they even are, if they don't have to..
		glDetachShader(shaderProgram, shaders[i]);
	}

	return shaderProgram;
}

#undef MULTILINE_STRING

static qboolean
initShaderPostfx(gl3ShaderInfo_t* shaderInfo, const char* vertSrc, const char* fragSrc)
{
	GLuint shaders2D[2] = {0};
	GLuint prog = 0;

	if(shaderInfo->shaderProgram != 0)
	{
		R_Printf(PRINT_ALL, "WARNING: calling initShaderPostfx for gl3ShaderInfo_t that already has a shaderProgram!\n");
		glDeleteProgram(shaderInfo->shaderProgram);
	}

	//shaderInfo->uniColor = shaderInfo->uniProjMatrix = shaderInfo->uniModelViewMatrix = -1;
	shaderInfo->shaderProgram = 0;
	shaderInfo->uniLmScales = -1;

	shaders2D[0] = CompileShader(GL_VERTEX_SHADER, vertSrc, NULL);
	if(shaders2D[0] == 0)  return false;

	shaders2D[1] = CompileShader(GL_FRAGMENT_SHADER, fragSrc, NULL);
	if(shaders2D[1] == 0)
	{
		glDeleteShader(shaders2D[0]);
		return false;
	}

	prog = CreateShaderProgram(2, shaders2D);

	// I think the shaders aren't needed anymore once they're linked into the program
	glDeleteShader(shaders2D[0]);
	glDeleteShader(shaders2D[1]);

	if(prog == 0)
	{
		return false;
	}

	shaderInfo->shaderProgram = prog;
	GL3_UseProgram(prog);

	// Bind the buffer object to the uniform blocks
	GLuint blockIndex = glGetUniformBlockIndex(prog, "uniCommon");
	if(blockIndex != GL_INVALID_INDEX)
	{
		GLint blockSize;
		glGetActiveUniformBlockiv(prog, blockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
		if(blockSize != sizeof(gl3state.uniCommonData))
		{
			R_Printf(PRINT_ALL, "WARNING: OpenGL driver disagrees with us about UBO size of 'uniCommon': %i vs %i\n",
					blockSize, (int)sizeof(gl3state.uniCommonData));

			goto err_cleanup;
		}

		glUniformBlockBinding(prog, blockIndex, GL3_BINDINGPOINT_UNICOMMON);
	}
	else
	{
		R_Printf(PRINT_ALL, "WARNING: Couldn't find uniform block index 'uniCommon'\n");
		goto err_cleanup;
	}

	return true;

err_cleanup:

	glDeleteProgram(prog);

	return false;
}

static qboolean
initShader2D(gl3ShaderInfo_t* shaderInfo, const char* vertSrc, const char* fragSrc)
{
	GLuint shaders2D[2] = {0};
	GLuint prog = 0;

	if(shaderInfo->shaderProgram != 0)
	{
		R_Printf(PRINT_ALL, "WARNING: calling initShader2D for gl3ShaderInfo_t that already has a shaderProgram!\n");
		glDeleteProgram(shaderInfo->shaderProgram);
	}

	//shaderInfo->uniColor = shaderInfo->uniProjMatrix = shaderInfo->uniModelViewMatrix = -1;
	shaderInfo->shaderProgram = 0;
	shaderInfo->uniLmScales = -1;

	shaders2D[0] = CompileShader(GL_VERTEX_SHADER, vertSrc, NULL);
	if(shaders2D[0] == 0)  return false;

	shaders2D[1] = CompileShader(GL_FRAGMENT_SHADER, fragSrc, NULL);
	if(shaders2D[1] == 0)
	{
		glDeleteShader(shaders2D[0]);
		return false;
	}

	prog = CreateShaderProgram(2, shaders2D);

	// I think the shaders aren't needed anymore once they're linked into the program
	glDeleteShader(shaders2D[0]);
	glDeleteShader(shaders2D[1]);

	if(prog == 0)
	{
		return false;
	}

	shaderInfo->shaderProgram = prog;
	GL3_UseProgram(prog);

	// Bind the buffer object to the uniform blocks
	GLuint blockIndex = glGetUniformBlockIndex(prog, "uniCommon");
	if(blockIndex != GL_INVALID_INDEX)
	{
		GLint blockSize;
		glGetActiveUniformBlockiv(prog, blockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
		if(blockSize != sizeof(gl3state.uniCommonData))
		{
			R_Printf(PRINT_ALL, "WARNING: OpenGL driver disagrees with us about UBO size of 'uniCommon': %i vs %i\n",
					blockSize, (int)sizeof(gl3state.uniCommonData));

			goto err_cleanup;
		}

		glUniformBlockBinding(prog, blockIndex, GL3_BINDINGPOINT_UNICOMMON);
	}
	else
	{
		R_Printf(PRINT_ALL, "WARNING: Couldn't find uniform block index 'uniCommon'\n");
		// TODO: clean up?
		return false;
	}
	blockIndex = glGetUniformBlockIndex(prog, "uni2D");
	if(blockIndex != GL_INVALID_INDEX)
	{
		GLint blockSize;
		glGetActiveUniformBlockiv(prog, blockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
		if(blockSize != sizeof(gl3state.uni2DData))
		{
			R_Printf(PRINT_ALL, "WARNING: OpenGL driver disagrees with us about UBO size of 'uni2D'\n");
			goto err_cleanup;
		}

		glUniformBlockBinding(prog, blockIndex, GL3_BINDINGPOINT_UNI2D);
	}
	else
	{
		R_Printf(PRINT_ALL, "WARNING: Couldn't find uniform block index 'uni2D'\n");
		goto err_cleanup;
	}

	return true;

err_cleanup:

	glDeleteProgram(prog);

	return false;
}

static qboolean
initShader3D(gl3ShaderInfo_t* shaderInfo, const char* vertSrc, const char* fragSrc)
{
	GLuint shaders3D[2] = {0};
	GLuint prog = 0;
	int i=0;

	if(shaderInfo->shaderProgram != 0)
	{
		R_Printf(PRINT_ALL, "WARNING: calling initShader3D for gl3ShaderInfo_t that already has a shaderProgram!\n");
		glDeleteProgram(shaderInfo->shaderProgram);
	}

	shaderInfo->shaderProgram = 0;
	shaderInfo->uniLmScales = -1;

	shaders3D[0] = CompileShader(GL_VERTEX_SHADER, vertexCommon3D, vertSrc);
	if(shaders3D[0] == 0)  return false;

	shaders3D[1] = CompileShader(GL_FRAGMENT_SHADER, fragmentCommon3D, fragSrc);
	if(shaders3D[1] == 0)
	{
		glDeleteShader(shaders3D[0]);
		return false;
	}

	prog = CreateShaderProgram(2, shaders3D);

	if(prog == 0)
	{
		goto err_cleanup;
	}

	GL3_UseProgram(prog);

	// Bind the buffer object to the uniform blocks
	GLuint blockIndex = glGetUniformBlockIndex(prog, "uniCommon");
	if(blockIndex != GL_INVALID_INDEX)
	{
		GLint blockSize;
		glGetActiveUniformBlockiv(prog, blockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
		if(blockSize != sizeof(gl3state.uniCommonData))
		{
			R_Printf(PRINT_ALL, "WARNING: OpenGL driver disagrees with us about UBO size of 'uniCommon'\n");

			goto err_cleanup;
		}

		glUniformBlockBinding(prog, blockIndex, GL3_BINDINGPOINT_UNICOMMON);
	}
	else
	{
		R_Printf(PRINT_ALL, "WARNING: Couldn't find uniform block index 'uniCommon'\n");

		goto err_cleanup;
	}
	blockIndex = glGetUniformBlockIndex(prog, "uni3D");
	if(blockIndex != GL_INVALID_INDEX)
	{
		GLint blockSize;
		glGetActiveUniformBlockiv(prog, blockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
		if(blockSize != sizeof(gl3state.uni3DData))
		{
			R_Printf(PRINT_ALL, "WARNING: OpenGL driver disagrees with us about UBO size of 'uni3D'\n");
			R_Printf(PRINT_ALL, "         driver says %d, we expect %d\n", blockSize, (int)sizeof(gl3state.uni3DData));

			goto err_cleanup;
		}

		glUniformBlockBinding(prog, blockIndex, GL3_BINDINGPOINT_UNI3D);
	}
	else
	{
		R_Printf(PRINT_ALL, "WARNING: Couldn't find uniform block index 'uni3D'\n");

		goto err_cleanup;
	}
	blockIndex = glGetUniformBlockIndex(prog, "uniLights");
	if(blockIndex != GL_INVALID_INDEX)
	{
		GLint blockSize;
		glGetActiveUniformBlockiv(prog, blockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
		if(blockSize != sizeof(gl3state.uniLightsData))
		{
			R_Printf(PRINT_ALL, "WARNING: OpenGL driver disagrees with us about UBO size of 'uniLights'\n");
			R_Printf(PRINT_ALL, "         OpenGL says %d, we say %d\n", blockSize, (int)sizeof(gl3state.uniLightsData));

			goto err_cleanup;
		}

		glUniformBlockBinding(prog, blockIndex, GL3_BINDINGPOINT_UNILIGHTS);
	}
	// else: as uniLights is only used in the LM shaders, it's ok if it's missing

	blockIndex = glGetUniformBlockIndex(prog, "uniStyles");
	if(blockIndex != GL_INVALID_INDEX)
	{
		GLint blockSize;
		glGetActiveUniformBlockiv(prog, blockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
		if(blockSize != sizeof(gl3state.uniStylesData))
		{
			R_Printf(PRINT_ALL, "WARNING: OpenGL driver disagrees with us about UBO size of 'uniStyles'\n");
			R_Printf(PRINT_ALL, "         OpenGL says %d, we say %d\n", blockSize, (int)sizeof(gl3state.uniStylesData));

			goto err_cleanup;
		}

		glUniformBlockBinding(prog, blockIndex, GL3_BINDINGPOINT_UNISTYLES);
	}

	// make sure texture is GL_TEXTURE0
	GLint texLoc = glGetUniformLocation(prog, "tex");
	if(texLoc != -1)
	{
		glUniform1i(texLoc, 0);
	}

	// ..  and the 4 lightmap texture use GL_TEXTURE1..4
	char lmName[10] = "lightmapX";
	for(i=0; i<4; ++i)
	{
		lmName[8] = '0'+i;
		GLint lmLoc = glGetUniformLocation(prog, lmName);
		if(lmLoc != -1)
		{
			glUniform1i(lmLoc, i+1); // lightmap0 belongs to GL_TEXTURE1, lightmap1 to GL_TEXTURE2 etc
		}
	}

	//eprintf("faceSelectionTex1: %d\n", glGetUniformLocation(prog, "faceSelectionTex1"));
	//eprintf("faceSelectionTex2: %d\n", glGetUniformLocation(prog, "faceSelectionTex2"));

	glUniform1i(glGetUniformLocation(prog, "shadowAtlasTex"), GL3_SHADOW_ATLAS_TU - GL_TEXTURE0);
	glUniform1i(glGetUniformLocation(prog, "shadowDebugColorTex"), GL3_SHADOW_DEBUG_COLOR_TU - GL_TEXTURE0);
	glUniform1i(glGetUniformLocation(prog, "faceSelectionTex1"), GL3_FACE_SELECTION1_TU - GL_TEXTURE0);
	glUniform1i(glGetUniformLocation(prog, "faceSelectionTex2"), GL3_FACE_SELECTION2_TU - GL_TEXTURE0);
	glUniform1i(glGetUniformLocation(prog, "ssao_sampler"), GL3_SSAO_MAP_TU - GL_TEXTURE0);

	GLint lmScalesLoc = glGetUniformLocation(prog, "lmScales");
	shaderInfo->uniLmScales = lmScalesLoc;
	if(lmScalesLoc != -1)
	{
		shaderInfo->lmScales[0] = HMM_Vec4(1.0f, 1.0f, 1.0f, 1.0f);

		for(i=1; i<4; ++i)  shaderInfo->lmScales[i] = HMM_Vec4(0.0f, 0.0f, 0.0f, 0.0f);

		glUniform4fv(lmScalesLoc, 4, shaderInfo->lmScales[0].Elements);
	}

	shaderInfo->shaderProgram = prog;

	// I think the shaders aren't needed anymore once they're linked into the program
	glDeleteShader(shaders3D[0]);
	glDeleteShader(shaders3D[1]);

	return true;

err_cleanup:

	glDeleteShader(shaders3D[0]);
	glDeleteShader(shaders3D[1]);

	if(prog != 0)  glDeleteProgram(prog);

	return false;
}

static void initUBOs(void)
{
	gl3state.uniCommonData.gamma = 1.0f/vid_gamma->value;
	gl3state.uniCommonData.intensity = gl3_intensity->value;
	gl3state.uniCommonData.intensity2D = gl3_intensity_2D->value;
	gl3state.uniCommonData.color = HMM_Vec4(1, 1, 1, 1);

	glGenBuffers(1, &gl3state.uniCommonUBO);
	glBindBuffer(GL_UNIFORM_BUFFER, gl3state.uniCommonUBO);
	glBindBufferBase(GL_UNIFORM_BUFFER, GL3_BINDINGPOINT_UNICOMMON, gl3state.uniCommonUBO);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(gl3state.uniCommonData), &gl3state.uniCommonData, GL_DYNAMIC_DRAW);

	// the matrix will be set to something more useful later, before being used
	gl3state.uni2DData.transMat4 = HMM_Mat4();

	glGenBuffers(1, &gl3state.uni2DUBO);
	glBindBuffer(GL_UNIFORM_BUFFER, gl3state.uni2DUBO);
	glBindBufferBase(GL_UNIFORM_BUFFER, GL3_BINDINGPOINT_UNI2D, gl3state.uni2DUBO);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(gl3state.uni2DData), &gl3state.uni2DData, GL_DYNAMIC_DRAW);

	// the matrices will be set to something more useful later, before being used
	gl3state.uni3DData.transProjMat4 = HMM_Mat4();
	gl3state.uni3DData.transViewMat4 = HMM_Mat4();
	gl3state.uni3DData.transModelMat4 = gl3_identityMat4;
	gl3state.uni3DData.scroll = 0.0f;
	gl3state.uni3DData.time = 0.0f;
	gl3state.uni3DData.alpha = 1.0f;
	// gl3_overbrightbits 0 means "no scaling" which is equivalent to multiplying with 1
	gl3state.uni3DData.overbrightbits = (gl3_overbrightbits->value <= 0.0f) ? 1.0f : gl3_overbrightbits->value;
	gl3state.uni3DData.particleFadeFactor = gl3_particle_fade_factor->value;

	glGenBuffers(1, &gl3state.uni3DUBO);
	glBindBuffer(GL_UNIFORM_BUFFER, gl3state.uni3DUBO);
	glBindBufferBase(GL_UNIFORM_BUFFER, GL3_BINDINGPOINT_UNI3D, gl3state.uni3DUBO);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(gl3state.uni3DData), &gl3state.uni3DData, GL_DYNAMIC_DRAW);

	glGenBuffers(1, &gl3state.uniLightsUBO);
	glBindBuffer(GL_UNIFORM_BUFFER, gl3state.uniLightsUBO);
	glBindBufferBase(GL_UNIFORM_BUFFER, GL3_BINDINGPOINT_UNILIGHTS, gl3state.uniLightsUBO);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(gl3state.uniLightsData), &gl3state.uniLightsData, GL_DYNAMIC_DRAW);

	glGenBuffers(1, &gl3state.uniStylesUBO);
	glBindBuffer(GL_UNIFORM_BUFFER, gl3state.uniStylesUBO);
	glBindBufferBase(GL_UNIFORM_BUFFER, GL3_BINDINGPOINT_UNISTYLES, gl3state.uniStylesUBO);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(gl3state.uniStylesData), &gl3state.uniStylesData, GL_DYNAMIC_DRAW);

	gl3state.currentUBO = gl3state.uniStylesUBO;
}

static qboolean createShaders(void)
{
	eprintf("si2D\n");
	if(!initShader2D(&gl3state.si2D, vertexSrc2D, fragmentSrc2D))
	{
		R_Printf(PRINT_ALL, "WARNING: Failed to create shader program for textured 2D rendering!\n");
		return false;
	}
	eprintf("si2Dcolor\n");
	if(!initShader2D(&gl3state.si2Dcolor, vertexSrc2Dcolor, fragmentSrc2Dcolor))
	{
		R_Printf(PRINT_ALL, "WARNING: Failed to create shader program for color-only 2D rendering!\n");
		return false;
	}
	eprintf("si3Dlm\n");
	if(!initShader3D(&gl3state.si3Dlm, vertexSrc3Dlm, fragmentSrc3Dlm))
	{
		R_Printf(PRINT_ALL, "WARNING: Failed to create shader program for textured 3D rendering with lightmap!\n");
		return false;
	}
	eprintf("si3DlmTurb\n");
	if(!initShader3D(&gl3state.si3DlmTurb, vertexSrc3Dlm, fragmentSrc3DlmWater))
	{
		R_Printf(PRINT_ALL, "WARNING: Failed to create shader program for textured 3D rendering water with lightmap!\n");
		return false;
	}
	eprintf("si3DTrans\n");
	if(!initShader3D(&gl3state.si3Dtrans, vertexSrc3D, fragmentSrc3D))
	{
		R_Printf(PRINT_ALL, "WARNING: Failed to create shader program for rendering translucent 3D things!\n");
		return false;
	}
	eprintf("si3DColorOnly\n");
	if(!initShader3D(&gl3state.si3DcolorOnly, vertexSrc3D, fragmentSrc3Dcolor))
	{
		R_Printf(PRINT_ALL, "WARNING: Failed to create shader program for flat-colored 3D rendering!\n");
		return false;
	}
	eprintf("si3DTurb\n");
	if(!initShader3D(&gl3state.si3Dturb, vertexSrc3Dwater, fragmentSrc3Dwater))
	{
		R_Printf(PRINT_ALL, "WARNING: Failed to create shader program for water rendering!\n");
		return false;
	}
	eprintf("si3DlmFlow\n");
	if(!initShader3D(&gl3state.si3DlmFlow, vertexSrc3DlmFlow, fragmentSrc3Dlm))
	{
		R_Printf(PRINT_ALL, "WARNING: Failed to create shader program for scrolling textured 3D rendering with lightmap!\n");
		return false;
	}
	eprintf("si3DTransFlow\n");
	if(!initShader3D(&gl3state.si3DtransFlow, vertexSrc3Dflow, fragmentSrc3D))
	{
		R_Printf(PRINT_ALL, "WARNING: Failed to create shader program for scrolling textured translucent 3D rendering!\n");
		return false;
	}
	eprintf("si3Dsky\n");
	if(!initShader3D(&gl3state.si3Dsky, vertexSrc3D, fragmentSrc3Dsky))
	{
		R_Printf(PRINT_ALL, "WARNING: Failed to create shader program for sky rendering!\n");
		return false;
	}
	eprintf("si3Dsprite\n");
	if(!initShader3D(&gl3state.si3Dsprite, vertexSrc3D, fragmentSrc3Dsprite))
	{
		R_Printf(PRINT_ALL, "WARNING: Failed to create shader program for sprite rendering!\n");
		return false;
	}
	eprintf("si3DSpiretAlpha\n");
	if(!initShader3D(&gl3state.si3DspriteAlpha, vertexSrc3D, fragmentSrc3DspriteAlpha))
	{
		R_Printf(PRINT_ALL, "WARNING: Failed to create shader program for alpha-tested sprite rendering!\n");
		return false;
	}
	eprintf("si3Dalias\n");
	if(!initShader3D(&gl3state.si3Dalias, vertexSrcAlias, fragmentSrcAlias))
	{
		R_Printf(PRINT_ALL, "WARNING: Failed to create shader program for rendering textured models!\n");
		return false;
	}
	eprintf("si3DaliasColor\n");
	if(!initShader3D(&gl3state.si3DaliasColor, vertexSrcAlias, fragmentSrcAliasColor))
	{
		R_Printf(PRINT_ALL, "WARNING: Failed to create shader program for rendering flat-colored models!\n");
		return false;
	}
	eprintf("siShadowMap\n");
	if(!initShader3D(&gl3state.siShadowMap, vertexSrcShadowMap, fragmentSrcShadowMap))
	{
		R_Printf(PRINT_ALL, "WARNING: Failed to create shader program for rendering world shadow map!\n");
		return false;
	}
	eprintf("si3DSSAO\n");
	if(!initShader3D(&gl3state.si3DSSAO, vertexSrc3DSSAO, fragmentSrc3DSSAO))
	{
		R_Printf(PRINT_ALL, "WARNING: Failed to create shader program for rendering world shadow map!\n");
		return false;
	}
	eprintf("si3DDebug\n");
	if(!initShader3D(&gl3state.si3Ddebug, vertexSrc3Ddebug, fragmentSrc3Ddebug))
	{
		R_Printf(PRINT_ALL, "WARNING: Failed to create shader program for rendering world shadow map!\n");
		return false;
	}

	const char* particleFrag = fragmentSrcParticles;
	if(gl3_particle_square->value != 0.0f)
	{
		particleFrag = fragmentSrcParticlesSquare;
	}

	if(!initShader3D(&gl3state.siParticle, vertexSrcParticles, particleFrag))
	{
		R_Printf(PRINT_ALL, "WARNING: Failed to create shader program for rendering particles!\n");
		return false;
	}


	if (!initShaderPostfx(&gl3state.siPostfxResolveMultisample, vertexSrcPostfxCommon, fragmentSrcPostfxResolveMultisample))
	{
		R_Printf(PRINT_ALL, "WARNING: Failed to create shader program for resolve multisample postfx!\n");
		return false;
	}
	if (!initShaderPostfx(&gl3state.siPostfxResolveHDR, vertexSrcPostfxCommon, fragmentSrcPostfxResolveHDR))
	{
		R_Printf(PRINT_ALL, "WARNING: Failed to create shader program for resolve HDR postfx!\n");
		return false;
	}
	if (!initShaderPostfx(&gl3state.siPostfxBloomFilter, vertexSrcPostfxCommon, fragmentSrcPostfxBloomFilter))
	{
		R_Printf(PRINT_ALL, "WARNING: Failed to create shader program for bloom filter postfx!\n");
		return false;
	}
	if (!initShaderPostfx(&gl3state.siPostfxBloomBlur, vertexSrcPostfxCommon, fragmentSrcPostfxBloomBlur))
	{
		R_Printf(PRINT_ALL, "WARNING: Failed to create shader program for bloom blur postfx!\n");
		return false;
	}
	if (!initShaderPostfx(&gl3state.siPostfxSSAO, vertexSrcPostfxSSAO, fragmentSrcPostfxSSAO))
	{
		R_Printf(PRINT_ALL, "WARNING: Failed to create shader program for SSAO postfx!\n");
		return false;
	}
	if (!initShaderPostfx(&gl3state.siPostfxSSAOBlur, vertexSrcPostfxCommon, fragmentSrcPostfxSSAOBlur))
	{
		R_Printf(PRINT_ALL, "WARNING: Failed to create shader program for SSAO postfx!\n");
		return false;
	}
	if (!initShaderPostfx(&gl3state.siPostfxMotionBlur, vertexSrcPostfxCommon, fragmentSrcPostfxMotionBlur))
	{
		R_Printf(PRINT_ALL, "WARNING: Failed to create shader program for motion blur postfx!\n");
		return false;
	}
	if (!initShaderPostfx(&gl3state.siPostfxBlit, vertexSrcPostfxCommon, fragmentSrcPostfxBlit))
	{
		R_Printf(PRINT_ALL, "WARNING: Failed to create shader program for motion blur postfx!\n");
		return false;
	}

	gl3state.currentShaderProgram = 0;

	return true;
}

qboolean GL3_InitShaders(void)
{
	initUBOs();

	return createShaders();
}

static void deleteShaders(void)
{
	const gl3ShaderInfo_t siZero = {0};
	for(gl3ShaderInfo_t* si = &gl3state.si2D; si < &gl3state.siSentinel; ++si)
	{
		if(si->shaderProgram != 0)  glDeleteProgram(si->shaderProgram);
		*si = siZero;
	}
}

void GL3_ShutdownShaders(void)
{
	deleteShaders();

	// let's (ab)use the fact that all 4 UBO handles are consecutive fields
	// of the gl3state struct
	glDeleteBuffers(5, &gl3state.uniCommonUBO);
	gl3state.uniCommonUBO = gl3state.uni2DUBO = gl3state.uni3DUBO = gl3state.uniLightsUBO = gl3state.uniStylesUBO = 0;
}

qboolean GL3_RecreateShaders(void)
{
	// delete and recreate the existing shaders (but not the UBOs)
	deleteShaders();
	return createShaders();
}

static inline void
updateUBO(GLuint ubo, GLsizeiptr size, void* data)
{
	if(gl3state.currentUBO != ubo)
	{
		gl3state.currentUBO = ubo;
		glBindBuffer(GL_UNIFORM_BUFFER, ubo);
	}

	// http://docs.gl/gl3/glBufferSubData says  "When replacing the entire data store,
	// consider using glBufferSubData rather than completely recreating the data store
	// with glBufferData. This avoids the cost of reallocating the data store."
	// no idea why glBufferData() doesn't just do that when size doesn't change, but whatever..
	// however, it also says glBufferSubData() might cause a stall so I DON'T KNOW!
	// on Linux/nvidia, by just looking at the fps, glBufferData() and glBufferSubData() make no difference
	// TODO: STREAM instead DYNAMIC?

#if 1
	// this seems to be reasonably fast everywhere.. glMapBuffer() seems to be a bit faster on OSX though..
	glBufferData(GL_UNIFORM_BUFFER, size, data, GL_DYNAMIC_DRAW);
#elif 0
	// on OSX this is super slow (200fps instead of 470-500), BUT it is as fast as glBufferData() when orphaning first
	// nvidia/linux-blob doesn't care about this vs glBufferData()
	// AMD open source linux (R3 370) is also slower here (not as bad as OSX though)
	// intel linux doesn't seem to care either (maybe 3% faster, but that might be imagination)
	// AMD Windows legacy driver (Radeon HD 6950) doesn't care, all 3 alternatives seem to be equally fast
	//glBufferData(GL_UNIFORM_BUFFER, size, NULL, GL_DYNAMIC_DRAW); // orphan
	glBufferSubData(GL_UNIFORM_BUFFER, 0, size, data);
#else
	// with my current nvidia-driver (GTX 770, 375.39), the following *really* makes it slower. (<140fps instead of ~850)
	// on OSX (Intel Haswell Iris Pro, OSX 10.11) this is fastest (~500fps instead of ~470)
	// on Linux/intel (Ivy Bridge HD-4000, Linux 4.4) this might be a tiny bit faster than the alternatives..
	glBufferData(GL_UNIFORM_BUFFER, size, NULL, GL_DYNAMIC_DRAW); // orphan
	GLvoid* ptr = glMapBuffer(GL_UNIFORM_BUFFER, GL_WRITE_ONLY);
	memcpy(ptr, data, size);
	glUnmapBuffer(GL_UNIFORM_BUFFER);
#endif

	// TODO: another alternative: glMapBufferRange() and each time update a different part
	//       of buffer asynchronously (GL_MAP_UNSYNCHRONIZED_BIT) => ringbuffer style
	//       when starting again from the beginning, synchronization must happen I guess..
	//       also, orphaning might be necessary
	//       and somehow make sure the new range is used by the UBO => glBindBufferRange()
	//  see http://git.quintin.ninja/mjones/Dolphin/blob/4a463f4588e2968c499236458c5712a489622633/Source/Plugins/Plugin_VideoOGL/Src/ProgramShaderCache.cpp#L207
	//   or https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoBackends/OGL/ProgramShaderCache.cpp
}

void GL3_UpdateUBOCommon(void)
{
	updateUBO(gl3state.uniCommonUBO, sizeof(gl3state.uniCommonData), &gl3state.uniCommonData);
}

void GL3_UpdateUBO2D(void)
{
	updateUBO(gl3state.uni2DUBO, sizeof(gl3state.uni2DData), &gl3state.uni2DData);
}

void GL3_UpdateUBO3D(void)
{
	updateUBO(gl3state.uni3DUBO, sizeof(gl3state.uni3DData), &gl3state.uni3DData);
}

void GL3_UpdateUBOLights(void)
{
	updateUBO(gl3state.uniLightsUBO, sizeof(gl3state.uniLightsData), &gl3state.uniLightsData);
}

void GL3_UpdateUBOStyles(void)
{
	updateUBO(gl3state.uniStylesUBO, sizeof(gl3state.uniStylesData), &gl3state.uniStylesData);
}

