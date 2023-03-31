//
// 3D Debug renderer
//

static const char* vertexSrc3Ddebug = MULTILINE_STRING(
		// it gets attributes and uniforms from vertexCommon3D

		out vec4 passColor;

		void main()
		{
			vec4 worldCoord = transModel * vec4(position, 1.0);
			gl_Position = transProj * transView * worldCoord;
			passColor = vec4(vertColor.rgb, 1.0);
		}
);

static const char* fragmentSrc3Ddebug = MULTILINE_STRING(
		in vec4 passColor;

		void main()
		{
			outColor = passColor;
		}
);
