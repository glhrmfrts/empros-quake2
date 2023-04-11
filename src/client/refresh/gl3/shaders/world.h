static const char* vertexSrc3Dlm = MULTILINE_STRING(

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
			passTexCoord = texCoord;
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

static const char* fragmentSrc3Dlm = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		void main()
		{
			vec4 texel = texture(tex, passTexCoord);
			vec4 albedo = texel;

			// apply intensity
			texel.rgb *= intensity;

			float actualEmit = emission*0.1f;

			// apply lightmap
			vec4 lmTex = (texture(lightmap0, passLMcoord)*(1.0f+actualEmit)) * lightstyles[pass_style0];
			lmTex     += (texture(lightmap1, passLMcoord)*(1.0f+actualEmit)) * lightstyles[pass_style1];
			lmTex     += (texture(lightmap2, passLMcoord)*(1.0f+actualEmit)) * lightstyles[pass_style2];
			lmTex     += (texture(lightmap3, passLMcoord)*(1.0f+actualEmit)) * lightstyles[pass_style3];

			if (ssao == 1.0f)
			{
				vec2 fragCoord = gl_FragCoord.xy / textureSize(ssao_sampler, 0).xy;
				fragCoord *= 0.5;
				lmTex.rgb *= texture(ssao_sampler, fragCoord).rgb;
			}

			lmTex.rgb += CalculateDLighting();

			lmTex.rgb *= overbrightbits;
			outColor = lmTex*texel;

			float fogDensity = fogParams.w/64.0;
			float fog = exp(-fogDensity * fogDensity * passFogCoord * passFogCoord);
			fog = clamp(fog, 0.0, 1.0);
			outColor.rgb = mix(fogParams.xyz, outColor.rgb, fog);

			//outColor.rgb = pow(outColor.rgb, vec3(gamma)); // apply gamma correction to result

			outColor.a = 1; // lightmaps aren't used with translucent surfaces
		}
);