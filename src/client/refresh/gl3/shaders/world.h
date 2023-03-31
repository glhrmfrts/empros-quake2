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

		uniform sampler2DShadow shadowAtlasTex;
		uniform sampler2D shadowDebugColorTex;
		uniform samplerCube faceSelectionTex1;
		uniform samplerCube faceSelectionTex2;

		in vec2 passLMcoord;
		in vec3 passWorldCoord;
		in vec3 passNormal;
		flat in uint passLightFlags;

		flat in uint pass_style0;
		flat in uint pass_style1;
		flat in uint pass_style2;
		flat in uint pass_style3;

		float SampleShadowMap(sampler2DShadow shadowTex, vec4 shadowPos, vec4 parameters)
		{
			vec4 offsets1 = vec4(2.0 * parameters.xy * shadowPos.w, 0.0, 0.0);
			vec4 offsets2 = vec4(2.0 * parameters.x * shadowPos.w, -2.0 * parameters.y * shadowPos.w, 0.0, 0.0);
			vec4 offsets3 = vec4(2.0 * parameters.x * shadowPos.w, 0.0, 0.0, 0.0);
			vec4 offsets4 = vec4(0.0, 2.0 * parameters.y * shadowPos.w, 0.0, 0.0);

			return smoothstep(0.0, 1.0, (
				textureProjLod(shadowTex, shadowPos, 0.0) +
				textureProjLod(shadowTex, shadowPos + offsets1, 0.0) +
				textureProjLod(shadowTex, shadowPos - offsets1, 0.0) +
				textureProjLod(shadowTex, shadowPos + offsets2, 0.0) +
				textureProjLod(shadowTex, shadowPos - offsets2, 0.0) +
				textureProjLod(shadowTex, shadowPos + offsets3, 0.0) +
				textureProjLod(shadowTex, shadowPos - offsets3, 0.0) +
				textureProjLod(shadowTex, shadowPos + offsets4, 0.0) +
				textureProjLod(shadowTex, shadowPos - offsets4, 0.0)
			) * 0.1111);
		}

		vec4 DebugShadowMapColor(vec4 shadowPos)
		{
			return textureProjLod(shadowDebugColorTex, shadowPos, 0.0);
		}

		vec4 GetPointShadowPos(uint index, vec3 lightVec)
		{
			vec4 pointParameters = dynLights[index].shadowMatrix[0];
			vec4 pointParameters2 = dynLights[index].shadowMatrix[1];
			float zoom = 1.0;
			float q = pointParameters2.y;
			float r = pointParameters2.z;

			vec3 axis = textureLod(faceSelectionTex1, lightVec.xzy, 0.0).xyz;
			vec4 adjust = textureLod(faceSelectionTex2, lightVec.xzy, 0.0);

			float depth = abs(dot(lightVec.xzy, axis));
			vec3 normLightVec = (lightVec.xzy / depth) * zoom;
			vec2 coords = vec2(dot(normLightVec.zxx, axis), dot(normLightVec.yzy, axis)) * adjust.xy + adjust.zw;
			coords = coords * pointParameters.xy + pointParameters.zw;
			float bias = 0.0001f;
			return vec4(coords, (q + r / depth) - bias, 1.0);
		}

		float Square(float x) { return x*x; }

		void main()
		{
			vec4 texel = texture(tex, passTexCoord);
			vec4 albedo = texel;

			// apply intensity
			texel.rgb *= intensity;

			// apply lightmap
			vec4 lmTex = (texture(lightmap0, passLMcoord)*(1.0f+emission)) * lightstyles[pass_style0];
			lmTex     += (texture(lightmap1, passLMcoord)*(1.0f+emission)) * lightstyles[pass_style1];
			lmTex     += (texture(lightmap2, passLMcoord)*(1.0f+emission)) * lightstyles[pass_style2];
			lmTex     += (texture(lightmap3, passLMcoord)*(1.0f+emission)) * lightstyles[pass_style3];

			if (ssao == 1.0f)
			{
				vec2 fragCoord = gl_FragCoord.xy / textureSize(ssao_sampler, 0).xy;
				fragCoord *= 0.5;
				lmTex.rgb *= texture(ssao_sampler, fragCoord).rgb;
			}

			// TODO: or is hardcoding 32 better?
			for(uint i=0u; i<numDynLights; ++i)
			{
				// I made the following up, it's probably not too cool..
				// it basically checks if the light is on the right side of the surface
				// and, if it is, sets intensity according to distance between light and pixel on surface

				// dyn light number i does not affect this plane, just skip it
				// if((passLightFlags & (1u << i)) == 0u)  continue;

				// TODO: store attenuation in the uniform buffer
				float intens = dynLights[i].lightColor.a;
				float atten = 1.0f / max(0.00001f, intens*intens);

				vec3 lightToPos = dynLights[i].lightOrigin - passWorldCoord;
				float distanceSqr = max(0.00001f, dot(lightToPos, lightToPos));

				float rangeAtten = Square(
					clamp(1.0 - Square(distanceSqr * atten), 0.0, 1.0)
				);
				float fact = rangeAtten;

				// move the light source a bit further above the surface
				// => helps if the lightsource is so close to the surface (e.g. grenades, rockets)
				//    that the dot product below would return 0
				// (light sources that are below the surface are filtered out by lightFlags)
				vec3 raisedLightToPos = lightToPos + passNormal*32.0;
				float NdotL = max(0, dot(passNormal, normalize(raisedLightToPos)));

				vec4 shadowParams = dynLights[i].shadowParameters;
				vec3 shadowDebugColor = vec3(0.0);
				if (shadowParams.z < 1.0)
				{
					vec4 shadowPos = GetPointShadowPos(i, lightToPos);
					fact *= clamp(SampleShadowMap(shadowAtlasTex, shadowPos, shadowParams), 0.0, 1.0);
					shadowDebugColor = DebugShadowMapColor(shadowPos).rgb;
				}

				lmTex.rgb += dynLights[i].lightColor.rgb * fact * NdotL;
				//lmTex.rgb += shadowDebugColor * fact;
			}

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