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

		layout (std140) uniform uniLights
		{
			DynLight dynLights[32];
			uint numDynLights;
			uint _pad1; uint _pad2; uint _pad3; // FFS, AMD!
		};

		layout (std140) uniform uniStyles
		{
			vec4 lightstyles[256];
		};

		uniform sampler2D tex;

		uniform sampler2D lightmap0;
		uniform sampler2D lightmap1;
		uniform sampler2D lightmap2;
		uniform sampler2D lightmap3;

		in vec2 passLMcoord;
		in vec3 passWorldCoord;
		in vec3 passNormal;
		flat in uint passLightFlags;

		flat in uint pass_style0;
		flat in uint pass_style1;
		flat in uint pass_style2;
		flat in uint pass_style3;

		void main()
		{
			const float MPI = 3.14159;

			vec2 ntc = passTexCoord;
			ntc.s += 0.125 + sin( passTexCoord.t*MPI + (((time*20.0)*MPI*2.0)/128.0) ) * 0.125;
			//ntc.s += scroll;
			ntc.t += 0.125 + sin( passTexCoord.s*MPI + (((time*20.0)*MPI*2.0)/128.0) ) * 0.125;
			// tc *= 1.0/64.0; // do this last

			vec4 texel = texture(tex, ntc);
			vec4 albedo = texel;

			// apply intensity
			texel.rgb *= intensity;

			// apply lightmap
			vec4 lmTex = texture(lightmap0, passLMcoord) * lightstyles[pass_style0];
			lmTex     += texture(lightmap1, passLMcoord) * lightstyles[pass_style1];
			lmTex     += texture(lightmap2, passLMcoord) * lightstyles[pass_style2];
			lmTex     += texture(lightmap3, passLMcoord) * lightstyles[pass_style3];

			if(true)
			{
				// TODO: or is hardcoding 32 better?
				for(uint i=0u; i<numDynLights; ++i)
				{
					// I made the following up, it's probably not too cool..
					// it basically checks if the light is on the right side of the surface
					// and, if it is, sets intensity according to distance between light and pixel on surface

					// dyn light number i does not affect this plane, just skip it
					// if((passLightFlags & (1u << i)) == 0u)  continue;

					float intens = dynLights[i].lightColor.a;

					vec3 lightToPos = dynLights[i].lightOrigin - passWorldCoord;
					float distLightToPos = length(lightToPos);
					float fact = max(0, intens - distLightToPos - 52);

					// move the light source a bit further above the surface
					// => helps if the lightsource is so close to the surface (e.g. grenades, rockets)
					//    that the dot product below would return 0
					// (light sources that are below the surface are filtered out by lightFlags)
					lightToPos += passNormal*32.0;

					// also factor in angle between light and point on surface
					fact *= max(0, dot(passNormal, normalize(lightToPos)));


					lmTex.rgb += dynLights[i].lightColor.rgb * fact * (1.0/256.0);
				}
			}

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

		uniform sampler2D tex;

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