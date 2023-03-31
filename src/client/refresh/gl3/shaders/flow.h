static const char* vertexSrc3Dflow = MULTILINE_STRING(

		// it gets attributes and uniforms from vertexCommon3D

		void main()
		{
			passTexCoord = texCoord + vec2(scroll, 0);
			gl_Position = transProj * transView * transModel * vec4(position, 1.0);
			passFogCoord = gl_Position.w;
		}
);

static const char* vertexSrc3DlmFlow = MULTILINE_STRING(

		// it gets attributes and uniforms from vertexCommon3D

		out vec2 passLMcoord;
		out vec3 passWorldCoord;
		out vec3 passNormal;
		flat out uint passLightFlags;

		flat out uint pass_style0;
		flat out uint pass_style1;
		flat out uint pass_style2;
		flat out uint pass_style3;

		void main()
		{
			passTexCoord = texCoord + vec2(scroll, 0);
			passLMcoord = lmTexCoord;
			vec4 worldCoord = transModel * vec4(position, 1.0);
			passWorldCoord = worldCoord.xyz;
			vec4 worldNormal = transModel * vec4(normal, 0.0f);
			passNormal = normalize(worldNormal.xyz);
			passLightFlags = lightFlags;

			pass_style0 = style0;
			pass_style1 = style1;
			pass_style2 = style2;
			pass_style3 = style3;

			gl_Position = transProj * transView * worldCoord;

			passFogCoord = gl_Position.w;
		}
);