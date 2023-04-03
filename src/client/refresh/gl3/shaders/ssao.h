//
// SSAO
//

static const char* vertexSrc3DSSAO = MULTILINE_STRING(
		// it gets attributes and uniforms from vertexCommon3D

		out vec3 passViewCoord;
		out vec3 passNormal;

		void main()
		{
			passTexCoord = texCoord;

			vec4 worldCoord = transModel * vec4(position, 1.0);
			passViewCoord = (transView * worldCoord).xyz;

			vec4 viewNormal = transView * transModel * vec4(normal, 0.0f);
			passNormal = normalize(viewNormal.xyz);

			gl_Position = transProj * transView * worldCoord;

			passFogCoord = gl_Position.w;
		}
);

static const char* fragmentSrc3DSSAO = MULTILINE_STRING(
		in vec3 passViewCoord;

		out vec4 outColor2;

		void main()
		{
			outColor = vec4(passViewCoord, 1.0);
			outColor2 = vec4(passNormal, 1.0);
		}
);