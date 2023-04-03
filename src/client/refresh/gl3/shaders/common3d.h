// ############## shaders for 3D rendering #####################

static const char* vertexCommon3D = MULTILINE_STRING(#version 150\n

		in vec3 position;   // GL3_ATTRIB_POSITION
		in vec2 texCoord;   // GL3_ATTRIB_TEXCOORD
		in vec2 lmTexCoord; // GL3_ATTRIB_LMTEXCOORD
		in vec4 vertColor;  // GL3_ATTRIB_COLOR
		in vec3 normal;     // GL3_ATTRIB_NORMAL
		in uint lightFlags; // GL3_ATTRIB_LIGHTFLAGS
		in uint style0;	    // GL3_ATTRIB_STYLE0
		in uint style1;	    // GL3_ATTRIB_STYLE1
		in uint style2;	    // GL3_ATTRIB_STYLE2
		in uint style3;	    // GL3_ATTRIB_STYLE3

		out vec2 passTexCoord;
		out float passFogCoord;

		// for UBO shared between all 3D shaders
		layout (std140) uniform uni3D
		{
			mat4 transProj;
			mat4 transView;
			mat4 transModel;

			float scroll; // for SURF_FLOWING
			float time;
			float alpha;
			float emission;
			float overbrightbits;
			float particleFadeFactor;

			float ssao;
			float _pad_1;

			vec4  fogParams; // .a is density, aligned at 16 bytes
		};
);

static const char* fragmentCommon3D = MULTILINE_STRING(#version 150\n

		in vec2 passTexCoord;
		in float passFogCoord;

		out vec4 outColor;

		uniform sampler2D ssao_sampler;

		// for UBO shared between all shaders (incl. 2D)
		layout (std140) uniform uniCommon
		{
			float gamma; // this is 1.0/vid_gamma
			float intensity;
			float intensity2D; // for HUD, menus etc

			vec4 color; // really?
		};

		// for UBO shared between all 3D shaders
		layout (std140) uniform uni3D
		{
			mat4 transProj;
			mat4 transView;
			mat4 transModel;

			float scroll; // for SURF_FLOWING
			float time;
			float alpha;
			float emission;
			float overbrightbits;
			float particleFadeFactor;
			float ssao;

			float _pad_1;

			vec4  fogParams; // .a is density, aligned at 16 bytes
		};

		struct DynLight { // gl3UniDynLight in C
			vec3 lightOrigin;
			float _pad;
			//vec3 lightColor;
			//float lightIntensity;
			vec4 lightColor; // .a is intensity; this way it also works on OSX...
			// (otherwise lightIntensity always contained 1 there)
			vec4 shadowParameters;
			mat4 shadowMatrix;
		};

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

		vec3 CalculateDLighting()
		{
			vec3 res = vec3(0.0);
			// TODO: or is hardcoding 32 better?
			for(uint i=0u; i<numDynLights; ++i)
			{
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

				res += dynLights[i].lightColor.rgb * fact * NdotL;
				//lmTex.rgb += shadowDebugColor * fact;
			}
			return res;
		}
);

static const char* vertexSrc3D = MULTILINE_STRING(

		// it gets attributes and uniforms from vertexCommon3D

		void main()
		{
			passTexCoord = texCoord;
			gl_Position = transProj * transView * transModel * vec4(position, 1.0);
			passFogCoord = gl_Position.w;
		}
);
