#include "header/local.h"

enum {
	MAX_VERTICES = 4096*4,
	MAX_INDICES = 4096*8,
};

static GLuint vaoDebug;
static GLuint vboDebug;
static GLuint iboDebug;

struct DebugVertex {
	vec3_t pos;
	vec3_t col;
	float pad;
};
static struct DebugVertex vertices[MAX_VERTICES];
static unsigned int indices[MAX_INDICES];

static unsigned int numVertices;
static unsigned int numIndices;

void GL3_Debug_Init()
{
	glGenVertexArrays(1, &vaoDebug);
	glGenBuffers(1, &vboDebug);
	glGenBuffers(1, &iboDebug);

	glBindVertexArray(vaoDebug);
	glBindBuffer(GL_ARRAY_BUFFER, vboDebug);

	glEnableVertexAttribArray(GL3_ATTRIB_POSITION);
	qglVertexAttribPointer(GL3_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE, 7*sizeof(float), 0);

	glEnableVertexAttribArray(GL3_ATTRIB_COLOR);
	qglVertexAttribPointer(GL3_ATTRIB_COLOR, 4, GL_FLOAT, GL_FALSE, 7*sizeof(float), 3*sizeof(GLfloat));
}

static unsigned int AddVertex(const vec3_t p, const vec3_t c)
{
	if (numVertices + 1 >= MAX_VERTICES)
		return 0;

	VectorCopy(p, vertices[numVertices].pos);
	VectorCopy(c, vertices[numVertices].col);
	return numVertices++;
}

static void AddIndex(unsigned int idx)
{
	if (numIndices + 1 >= MAX_INDICES)
		return;

	indices[numIndices++] = idx;
}

void GL3_Debug_AddLine(const vec3_t p1, const vec3_t p2, const vec3_t color)
{
	unsigned int start = AddVertex(p1, color);
	AddVertex(p2, color);
	AddIndex(start);
	AddIndex(start + 1);
}

static void SpherePoint(float radius, float theta, float phi, vec3_t ret)
{
	ret[0] = radius * sinf(theta) * sinf(phi);
	ret[1] = radius * cosf(phi);
	ret[2] = radius * cosf(theta) * sinf(phi);
}

void GL3_Debug_AddSphere(const vec3_t origin, float radius, const vec3_t color)
{
	for (float j = 0.0f; j < 180.0f; j += 45.0f)
	{
		for (float i = 0.0f; i < 360.0f; i += 45.0f)
		{
			unsigned startVertex = numVertices;

			vec3_t p;

			SpherePoint(radius, i, j, p);
			VectorAdd(origin, p, p);
			AddVertex(p, color);

			SpherePoint(radius, i + 45.0f, j, p);
			VectorAdd(origin, p, p);
			AddVertex(p, color);

			SpherePoint(radius, i, j + 45.0f, p);
			VectorAdd(origin, p, p);
			AddVertex(p, color);

			SpherePoint(radius, i + 45.0f, j + 45.0f, p);
			VectorAdd(origin, p, p);
			AddVertex(p, color);

			AddIndex(startVertex);
			AddIndex(startVertex + 1);

			AddIndex(startVertex + 2);
			AddIndex(startVertex + 3);

			AddIndex(startVertex);
			AddIndex(startVertex + 2);

			AddIndex(startVertex + 1);
			AddIndex(startVertex + 3);
		}
	}
}

void GL3_Debug_AddPlane(const vec3_t origin, const vec3_t normal, float size, const vec3_t color)
{
	float radius = size/2.0f;

	// Generate base vertices
	hmm_vec4 baseVertices[4] = {
		{-1.0f, -1.0f, 0.0f, 1.0f},
		{1.0f, -1.0f, 0.0f, 1.0f},
		{1.0f, 1.0f, 0.0f, 1.0f},
		{-1.0f, 1.0f, 0.0f, 1.0f},
	};

	// Get the angle and axis necessary to rotate the plane towards the given normal
	hmm_vec3 baseNormal = {0.0f, 0.0f, 1.0f};
	hmm_vec3 hnormal = {normal[0], normal[1], normal[2]};
	hmm_vec3 cross = HMM_Cross(baseNormal, hnormal);
	float angle = acosf(HMM_DotVec3(baseNormal, hnormal));
	hmm_mat4 rotation = HMM_Rotate(angle * 180.0f / M_PI, cross);

	// Generate rotated points
	unsigned int start = numVertices;
	for (int i = 0; i < 4; i++)
	{
		hmm_vec4 point = HMM_MultiplyMat4ByVec4(rotation, baseVertices[i]);
		vec3_t p = {point.X * radius, point.Y * radius, point.Z * radius};
		VectorAdd(origin, p, p);
		AddVertex(p, color);
	}

	AddIndex(start);
	AddIndex(start + 1);

	AddIndex(start + 1);
	AddIndex(start + 2);

	AddIndex(start + 2);
	AddIndex(start);

	AddIndex(start + 2);
	AddIndex(start + 3);

	AddIndex(start + 3);
	AddIndex(start);
}

void GL3_Debug_AddBox(const vec3_t mins, const vec3_t maxs, const vec3_t color)
{
	vec3_t center;
	VectorAdd(mins, maxs, center);
	VectorScale(center, 0.5f, center);

	vec3_t normals[] = {
		{1.0f, 0.0f, 0.0f},
		{-1.0f, 0.0f, 0.0f},
		{0.0f, 1.0f, 0.0f},
		{0.0f, -1.0f, 0.0f},
		{0.0f, 0.0f, 1.0f},
		{0.0f, 0.0f, -1.0f},
	};
	float sizes[] = {
		maxs[2] - mins[2],
		maxs[2] - mins[2],
		maxs[2] - mins[2],
	};
	for (int i = 0; i < 6; i++)
	{
		vec3_t plane;
		float size = sizes[i>>1];
		VectorMA(center, size*0.5f, normals[i], plane);
		GL3_Debug_AddPlane(plane, normals[i], size, color);
	}
}

void GL3_Debug_AddFrustum(const vec3_t color)
{
	unsigned startVertex = numVertices;
	hmm_vec4* frustumVertices = gl3state.viewParams.vertices;

	AddVertex((const vec_t*)&frustumVertices[0], color);
	AddVertex((const vec_t*)&frustumVertices[1], color);
	AddVertex((const vec_t*)&frustumVertices[2], color);
	AddVertex((const vec_t*)&frustumVertices[3], color);
	AddVertex((const vec_t*)&frustumVertices[4], color);
	AddVertex((const vec_t*)&frustumVertices[5], color);
	AddVertex((const vec_t*)&frustumVertices[6], color);
	AddVertex((const vec_t*)&frustumVertices[7], color);

	AddIndex(startVertex);
	AddIndex(startVertex + 1);

	AddIndex(startVertex + 1);
	AddIndex(startVertex + 2);

	AddIndex(startVertex + 2);
	AddIndex(startVertex + 3);

	AddIndex(startVertex + 3);
	AddIndex(startVertex);

	AddIndex(startVertex + 4);
	AddIndex(startVertex + 5);

	AddIndex(startVertex + 5);
	AddIndex(startVertex + 6);

	AddIndex(startVertex + 6);
	AddIndex(startVertex + 7);

	AddIndex(startVertex + 7);
	AddIndex(startVertex + 4);

	AddIndex(startVertex);
	AddIndex(startVertex + 4);

	AddIndex(startVertex + 1);
	AddIndex(startVertex + 5);

	AddIndex(startVertex + 2);
	AddIndex(startVertex + 6);

	AddIndex(startVertex + 3);
	AddIndex(startVertex + 7);
}

void GL3_Debug_Draw()
{
	GL3_UseProgram(gl3state.si3Ddebug.shaderProgram);

	gl3state.uni3DData.transModelMat4 = gl3_identityMat4;
	GL3_UpdateUBO3D();

	glBindBuffer(GL_ARRAY_BUFFER, vboDebug);
	glBufferData(GL_ARRAY_BUFFER, numVertices*sizeof(struct DebugVertex), (const void*)vertices, GL_DYNAMIC_DRAW);

	GL3_BindVAO(vaoDebug);
	GL3_BindEBO(iboDebug);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, numIndices*sizeof(unsigned int), (const void*)indices, GL_DYNAMIC_DRAW);

	glDrawElements(GL_LINES, numIndices, GL_UNSIGNED_INT, NULL);

	numVertices = 0;
	numIndices = 0;
}

void GL3_Debug_Shutdown()
{
	if (vaoDebug != 0)
		glDeleteVertexArrays(1, &vaoDebug);
	if (vboDebug != 0)
		glDeleteBuffers(1, &vboDebug);
	if (iboDebug != 0)
		glDeleteBuffers(1, &iboDebug);
}
