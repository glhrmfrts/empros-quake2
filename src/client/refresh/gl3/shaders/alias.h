static const char* vertexSrcAlias = MULTILINE_STRING(

		// it gets attributes and uniforms from vertexCommon3D

		out vec3 passWorldCoord;
		out vec3 passNormal;

		out vec4 passColor;

		void main()
		{
			vec4 worldCoord = transModel * vec4(position, 1.0);
			vec4 worldNormal = transModel * vec4(normal, 0.0f);

			passColor = vertColor*overbrightbits;
			passTexCoord = texCoord;
			passWorldCoord = worldCoord.xyz;
			passNormal = normalize(worldNormal.xyz);

			gl_Position = transProj * transView * worldCoord;

			passFogCoord = gl_Position.w;
		}
);

static const char* fragmentSrcAlias = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		in vec4 passColor;

		void main()
		{
			vec4 albedo = texture(tex, passTexCoord);
			vec4 texel = min(vec4(1.5), passColor);

			texel.rgb += CalculateDLighting();

			// apply gamma correction and intensity
			texel.rgb *= intensity;
			texel.a *= alpha; // is alpha even used here?

			outColor = albedo*texel;

			float fogDensity = fogParams.w/64.0;
			float fog = exp(-fogDensity * fogDensity * passFogCoord * passFogCoord);
			fog = clamp(fog, 0.0, 1.0);
			outColor.rgb = mix(fogParams.xyz, outColor.rgb, fog);

			//outColor.rgb = pow(outColor.rgb, vec3(gamma));

			outColor.a = albedo.a; // I think alpha shouldn't be modified by gamma and intensity
		}
);

static const char* fragmentSrcAliasColor = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		in vec4 passColor;

		void main()
		{
			vec4 texel = passColor;

			// apply gamma correction and intensity
			// texel.rgb *= intensity; // TODO: color-only rendering probably shouldn't use intensity?
			texel.a *= alpha; // is alpha even used here?

			outColor = texel;

			float fogDensity = fogParams.w/64.0;
			float fog = exp(-fogDensity * fogDensity * passFogCoord * passFogCoord);
			fog = clamp(fog, 0.0, 1.0);
			outColor.rgb = mix(fogParams.xyz, outColor.rgb, fog);

			//outColor.rgb = pow(outColor.rgb, vec3(gamma));
			outColor.a = texel.a; // I think alpha shouldn't be modified by gamma and intensity
		}
);