static const char* vertexSrcShadowMapBlit = MULTILINE_STRING(#version 150\n

		in vec2 position; // GL3_ATTRIB_POSITION
		in vec2 texCoord; // GL3_ATTRIB_TEXCOORD

		out vec2 passTexCoord;

		void main()
		{
			gl_Position = vec4(position, 0.0, 1.0);
			passTexCoord = texCoord;
		}
);

static const char* fragmentSrcShadowMapBlit = MULTILINE_STRING(#version 150\n
		in vec2 passTexCoord;

		uniform sampler2D tex;

		out vec4 outColor;

		void main()
		{
			float depth = texture(tex, passTexCoord).r;
			outColor = vec4(1.0);
			gl_FragDepth = depth;
		}
);
