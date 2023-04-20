static const char* vertexSrc3Dwater = MULTILINE_STRING(

		// it gets attributes and uniforms from vertexCommon3D
		void main()
		{
			passTexCoord = texCoord;

			gl_Position = transProj * transView * transModel * vec4(position, 1.0);
			passFogCoord = gl_Position.w;
		}
);

static const char* fragmentSrc3DlmWater = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		void main()
		{
			const float MPI = 3.14159;

			vec2 ntc = passTexCoord;
			ntc.s += 0.125 + sin( passTexCoord.t*MPI + (((time*20.0)*MPI*2.0)/128.0) ) * 0.125;
			//ntc.s += scroll;
			ntc.t += 0.125 + sin( passTexCoord.s*MPI + (((time*20.0)*MPI*2.0)/128.0) ) * 0.125;
			// tc *= 1.0/64.0; // do this last

			vec4 texel = texture(tex, ntc);

			// Simulate an effect as if the water has a normal map
			specularNormal = passNormal * vec3(0.0, 0.0, clamp(length(texel.rgb)*1.8, 0.0, 1.0));

			// apply intensity
			texel.rgb *= intensity;

			// apply lightmap
			vec4 lmTex = texture(lightmap0, passLMcoord) * lightstyles[pass_style0];
			lmTex     += texture(lightmap1, passLMcoord) * lightstyles[pass_style1];
			lmTex     += texture(lightmap2, passLMcoord) * lightstyles[pass_style2];
			lmTex     += texture(lightmap3, passLMcoord) * lightstyles[pass_style3];

			lmTex.rgb += CalculateDLighting();

			lmTex.rgb *= overbrightbits;
			outColor = lmTex*texel;

			float fogDensity = fogParams.w/64.0;
			float fog = exp(-fogDensity * fogDensity * passFogCoord * passFogCoord);
			fog = clamp(fog, 0.0, 1.0);
			outColor.rgb = mix(fogParams.xyz, outColor.rgb, fog);

			//outColor.rgb = pow(outColor.rgb, vec3(gamma)); // apply gamma correction to result

			outColor.a = alpha; // lightmaps aren't used with translucent surfaces (gnemeth: yes, they are)
		}
);

static const char* fragmentSrc3Dwater = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		void main()
		{
			vec2 tc = passTexCoord;
			tc.s += sin( passTexCoord.t*0.125 + time ) * 4;
			//tc.s += scroll;
			tc.t += sin( passTexCoord.s*0.125 + time ) * 4;
			tc *= 1.0/64.0; // do this last

			vec4 texel = texture(tex, tc);

			// apply intensity and gamma
			texel.rgb *= intensity*0.5;
			outColor.rgb = texel.rgb;

			float fogDensity = fogParams.w/64.0;
			float fog = exp(-fogDensity * fogDensity * passFogCoord * passFogCoord);
			fog = clamp(fog, 0.0, 1.0);
			outColor.rgb = mix(fogParams.xyz, outColor.rgb, fog);

			//outColor.rgb = pow(outColor.rgb, vec3(gamma));
			outColor.a = texel.a*alpha; // I think alpha shouldn't be modified by gamma and intensity
		}
);