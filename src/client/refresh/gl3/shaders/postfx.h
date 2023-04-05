//
// Post FX
//

static const char* vertexSrcPostfxCommon = MULTILINE_STRING(#version 150\n

	in vec3 position;
	in vec2 texCoord;

	out vec2 v_TexCoord;

	void main() {
		gl_Position = vec4(position, 1.0);
		v_TexCoord = texCoord;
	}
);

static const char* fragmentSrcPostfxResolveMultisample = MULTILINE_STRING(#version 150\n

	uniform sampler2DMS u_FboSampler0;
	uniform sampler2DMS u_DepthSampler0;
	uniform int u_SampleCount;
	uniform float u_Intensity; // HDR exposure
	uniform float u_HDR; // 0.0 or 1.0 for branchless toggling HDR

	in vec2 v_TexCoord;

	// for UBO shared between all shaders (incl. 2D)
	layout (std140) uniform uniCommon
	{
		float gamma;
		float intensity;
		float intensity2D; // for HUD, menu etc

		vec4 color;
	};

	out vec4 outColor[2];

	void main()
	{
		vec4 combinedColor = vec4(0.0f);
		vec4 combinedDepth = vec4(0.0f);

		for (int i = 0; i < u_SampleCount; i++)
		{
			combinedColor += texelFetch(u_FboSampler0, ivec2(gl_FragCoord.xy), i);
			combinedDepth += texelFetch(u_DepthSampler0, ivec2(gl_FragCoord.xy), i);
		}

		float invSampleCount = 1.0f / float(u_SampleCount);

		outColor[0] = invSampleCount * combinedColor;

		// write out the depth
		outColor[1] = invSampleCount * combinedDepth;
	}
);

static const char* fragmentSrcPostfxResolveHDR = MULTILINE_STRING(#version 150\n

	uniform sampler2D u_FboSampler0; // hdr scene
	uniform sampler2D u_FboSampler1; // bloom blurred texture
	uniform int u_SampleCount;
	uniform float u_Intensity; // Bloom intensity
	uniform float u_HDR; // HDR exposure
	uniform int u_HDRMode; // ACES, Neutral, Reinhard, Exposure

	in vec2 v_TexCoord;

	// for UBO shared between all shaders (incl. 2D)
	layout (std140) uniform uniCommon
	{
		float gamma;
		float intensity;
		float intensity2D; // for HUD, menu etc

		vec4 color;
	};

	out vec4 outColor[2];

	vec3 TonemapACES(in vec3 x)
	{
		float a = 2.51f;
		float b = 0.03f;
		float c = 2.43f;
		float d = 0.59f;
		float e = 0.14f;
		return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
	}

	vec3 TonemapExposure(in vec3 hdrColor)
	{
		float exposure = u_HDR;
		return (vec3(1.0) - exp(-hdrColor * exposure));
	}

	void main()
	{
		float bloomIntensity = u_Intensity;

		vec3 hdrColor = texture(u_FboSampler0, v_TexCoord).rgb;
		vec3 bloomColor = texture(u_FboSampler1, v_TexCoord).rgb;
		hdrColor += bloomColor * bloomIntensity;

		vec3 mapped = vec3(0.0);

		// tone mapping
		switch (u_HDRMode)
		{
		case 0:
			mapped = TonemapACES(hdrColor);
			break;
		case 3:
			mapped = TonemapExposure(hdrColor);
			break;
		}

		// gamma correction
		mapped = pow(mapped, vec3(gamma));

		outColor[0] = vec4(mapped, 1.0);
	}
);

static const char* vertexSrcPostfxSSAO = MULTILINE_STRING(#version 150\n

	in vec3 position;
	in vec2 texCoord;

	uniform float u_AspectRatio;
	uniform float u_TanHalfFOV;

	out vec2 v_TexCoord;
	out vec2 v_ViewRay;

	void main() {
		gl_Position = vec4(position, 1.0);
		v_TexCoord = texCoord;
		v_ViewRay.x = position.x * u_AspectRatio * u_TanHalfFOV;
		v_ViewRay.y = position.y * u_TanHalfFOV;
	}
);

static const char* fragmentSrcPostfxSSAO = MULTILINE_STRING(#version 150\n
	uniform sampler2D u_FboSampler0; // position
	uniform sampler2D u_FboSampler1; // normal
	uniform sampler2D u_FboSampler2; // noise
	//uniform sampler2D u_DepthSampler0; // depth

	uniform mat4 u_ProjectionMatrix;
	uniform float u_Intensity;

	uniform vec2 u_Size; // noise scale

	in vec2 v_TexCoord;
	in vec2 v_ViewRay;

	// for UBO shared between all shaders (incl. 2D)
	layout (std140) uniform uniCommon
	{
		float gamma;
		float intensity;
		float intensity2D; // for HUD, menu etc

		vec4 color;
	};

	const int MAX_KERNEL_SIZE = 64;

	layout (std140) uniform uniSSAO
	{
		vec3 kernel[MAX_KERNEL_SIZE]; // needs to be vec4 in CPU
	};

	out vec4 outColor;

	// float GetViewZ(in vec2 texCoord)
	// {
	// 	float depth = texture2D(u_DepthSampler0, texCoord).r;
	// 	float viewZ = u_ProjectionMatrix[3][2] / (2 * depth - 1 - u_ProjectionMatrix[2][2]);
	// 	return viewZ;
	// }

	vec3 GetViewPos(in vec2 texCoord)
	{
		//float viewZ = GetViewZ(texCoord);
		//return vec3(v_ViewRay.x * viewZ, v_ViewRay.y * viewZ, viewZ);

		return texture(u_FboSampler0, texCoord).xyz;
	}

	vec3 GetViewNormal(in vec2 texCoord)
	{
		return texture(u_FboSampler1, texCoord).xyz;
	}

	void main()
	{
		vec3 fragPos = GetViewPos(v_TexCoord);
		vec3 fragNormal = GetViewNormal(v_TexCoord);

		vec3 randomVec = texture(u_FboSampler2, v_TexCoord * u_Size).rgb;
		vec3 tangent   = normalize(randomVec - fragNormal * dot(randomVec, fragNormal));
		vec3 bitangent = cross(fragNormal, tangent);
		mat3 TBN       = mat3(tangent, bitangent, fragNormal);

		float AO = 0.0;

		float bias = 0.01;
		float radius = 4.0*u_Intensity;

		for (int i = 0 ; i < MAX_KERNEL_SIZE ; i++) {
			vec3 kernelPos = TBN * kernel[i];
			vec3 samplePos = fragPos + kernelPos * radius;
			vec4 offset = vec4(samplePos, 1.0);
			offset = u_ProjectionMatrix * offset;
			offset.xy /= offset.w;
			offset.xy = offset.xy * 0.5 + vec2(0.5);

			float sampleDepth = GetViewPos(offset.xy).z;

			float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos.z - sampleDepth));
			AO += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
		}

		AO = 1.0 - AO/64.0;

		outColor = vec4(pow(AO, 1.0));
	}
);

static const char* fragmentSrcPostfxMotionBlur = MULTILINE_STRING(#version 150\n

	uniform sampler2D u_FboSampler0; // color
	uniform sampler2D u_FboSampler1; // depth
	uniform sampler2D u_FboSampler2; // mask
	uniform float u_Intensity;
	uniform int u_SampleCount;

	uniform mat4 u_ViewProjectionInverseMatrix;
	uniform mat4 u_PreviousViewProjectionMatrix;

	in vec2 v_TexCoord;

	// for UBO shared between all shaders (incl. 2D)
	layout (std140) uniform uniCommon
	{
		float gamma;
		float intensity;
		float intensity2D; // for HUD, menu etc

		vec4 color;
	};

	out vec4 outColor[2];

	void main()
	{
		vec2 texCoord = v_TexCoord;

		// Get the depth buffer value at this pixel.
		float zOverW = texture2D(u_FboSampler1, v_TexCoord).r;

		// H is the viewport position at this pixel in the range -1 to 1.
		vec4 H = vec4(v_TexCoord.x * 2 - 1, (1 - v_TexCoord.y) * 2 - 1, zOverW, 1);

		// Transform by the view-projection inverse.
		vec4 D = u_ViewProjectionInverseMatrix * H;

		// Divide by w to get the world position.
		vec4 worldPos = D / D.w;

		// Current viewport position
		vec4 currentPos = H;

		// Use the world position, and transform by the previous view-    // projection matrix.
		vec4 previousPos = u_PreviousViewProjectionMatrix * worldPos;

		// Convert to nonhomogeneous points [-1,1] by dividing by w.
		previousPos /= previousPos.w;

		// Get the mask value
		float maskValue = 1.0f - texture2D(u_FboSampler2, v_TexCoord).a;

		// Use this frame's position and last frame's to compute the pixel    // velocity.
		vec4 velocity = (currentPos - previousPos)/2.f * u_Intensity * maskValue;

		// Get the initial color at this pixel.
		vec4 color = texture2D(u_FboSampler0, texCoord);
		texCoord += velocity.xy;

		int numSamples = u_SampleCount;
		int numAvgSamples = u_SampleCount;

		for (int i = 1; i < numSamples; ++i) {
			// Sample the color buffer along the velocity vector.
			vec4 currentColor = texture2D(u_FboSampler0, texCoord);

			float maskValue = 1.0f - texture2D(u_FboSampler2, texCoord).a;
			if (maskValue == 0.0f) { numAvgSamples--; }

			// Add the current color to our color sum.
			color += currentColor * maskValue;
			texCoord += velocity.xy;
		}

		// Average all of the samples to get the final blur color.
		outColor[0] = color * (1.0f / numAvgSamples);
		outColor[1] = texture2D(u_FboSampler1, v_TexCoord);
	}
);

static const char* fragmentSrcPostfxSSAOBlur = MULTILINE_STRING(#version 150\n

	// for UBO shared between all shaders (incl. 2D)
	layout (std140) uniform uniCommon
	{
		float gamma;
		float intensity;
		float intensity2D; // for HUD, menu etc

		vec4 commoncolor;
	};

	out vec4 FragColor;

	in vec2 v_TexCoord;

	uniform sampler2D u_FboSampler0;

	void main() {
		vec2 texelSize = 1.0 / vec2(textureSize(u_FboSampler0, 0));
		float result = 0.0;
		for (int x = -2; x < 2; ++x)
		{
			for (int y = -2; y < 2; ++y)
			{
				vec2 offset = vec2(float(x), float(y)) * texelSize;
				result += texture(u_FboSampler0, v_TexCoord + offset).r;
			}
		}
		FragColor = vec4(vec3(result / (4.0 * 4.0)), 1.0);
	}

);

static const char* fragmentSrcPostfxBloomFilter = MULTILINE_STRING(#version 150\n

	uniform sampler2D u_FboSampler0; // hdr scene
	uniform float u_Intensity; // Bloom threshold

	in vec2 v_TexCoord;

	// for UBO shared between all shaders (incl. 2D)
	layout (std140) uniform uniCommon
	{
		float gamma;
		float intensity;
		float intensity2D; // for HUD, menu etc

		vec4 color;
	};

	out vec4 outColor;

	void main()
	{
		vec3 hdrColor = texture(u_FboSampler0, v_TexCoord).rgb;

		float brightness = dot(hdrColor.rgb, vec3(0.2126, 0.7152, 0.0722));
		if(brightness > u_Intensity)
			outColor = vec4(hdrColor.rgb, 1.0);
		else
			outColor = vec4(0.0, 0.0, 0.0, 1.0);
	}
);

static const char* fragmentSrcPostfxBloomBlur = MULTILINE_STRING(#version 150\n

	// for UBO shared between all shaders (incl. 2D)
	layout (std140) uniform uniCommon
	{
		float gamma;
		float intensity;
		float intensity2D; // for HUD, menu etc

		vec4 commoncolor;
	};

	out vec4 FragColor;

	in vec2 v_TexCoord;

	uniform sampler2D u_FboSampler0;

	uniform int u_SampleCount;
	uniform float weight[5] = float[] (0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

	void main()
	{
		vec2 tex_offset = 1.0 / textureSize(u_FboSampler0, 0); // gets size of single texel
		vec3 result = texture(u_FboSampler0, v_TexCoord).rgb * weight[0]; // current fragment's contribution
		bool horizontal = u_SampleCount > 0;
		if(horizontal)
		{
			for(int i = 1; i < 5; ++i)
			{
				result += texture(u_FboSampler0, v_TexCoord + vec2(tex_offset.x * i, 0.0)).rgb * weight[i];
				result += texture(u_FboSampler0, v_TexCoord - vec2(tex_offset.x * i, 0.0)).rgb * weight[i];
			}
		}
		else
		{
			for(int i = 1; i < 5; ++i)
			{
				result += texture(u_FboSampler0, v_TexCoord + vec2(0.0, tex_offset.y * i)).rgb * weight[i];
				result += texture(u_FboSampler0, v_TexCoord - vec2(0.0, tex_offset.y * i)).rgb * weight[i];
			}
		}
		FragColor = vec4(result, 1.0);
	}

);

static const char* fragmentSrcPostfxBlend = MULTILINE_STRING(#version 150\n

	uniform sampler2D u_FboSampler0;

	in vec2 v_TexCoord;

	// for UBO shared between all shaders (incl. 2D)
	layout (std140) uniform uniCommon
	{
		float gamma;
		float intensity;
		float intensity2D; // for HUD, menu etc

		vec4 color; // used as the blend
	};

	out vec4 outColor;

	void main()
	{
		vec4 res = texture(u_FboSampler0, v_TexCoord);
		res.rgb = color.a * color.rgb + (1.0 - color.a)*res.rgb;
		outColor = res;
	}
);

static const char* fragmentSrcPostfxUnderwater = MULTILINE_STRING(#version 150\n
	// for UBO shared between all shaders (incl. 2D)
	// TODO: not needed here, remove?
	layout (std140) uniform uniCommon
	{
		float gamma;
		float intensity;
		float intensity2D; // for HUD, menu etc

		vec4 color;
	};

	const float PI = 3.14159265358979323846;

	uniform sampler2D u_FboSampler0;

	uniform float u_AspectRatio; // Used as time

	in vec2 v_TexCoord;

	out vec4 outColor;

	void main()
	{
		vec2 uv = v_TexCoord;
		float time = u_AspectRatio;
		vec4 v_blend = color;

		// warping based on vkquake2
		// here uv is always between 0 and 1 so ignore all that scrWidth and gl_FragCoord stuff
		//float sx = pc.scale - abs(pc.scrWidth  / 2.0 - gl_FragCoord.x) * 2.0 / pc.scrWidth;
		//float sy = pc.scale - abs(pc.scrHeight / 2.0 - gl_FragCoord.y) * 2.0 / pc.scrHeight;
		float sx = 1.0 - abs(0.5-uv.x)*2.0;
		float sy = 1.0 - abs(0.5-uv.y)*2.0;
		float xShift = 2.0 * time + uv.y * PI * 10.0;
		float yShift = 2.0 * time + uv.x * PI * 10.0;
		vec2 distortion = vec2(sin(xShift) * sx, sin(yShift) * sy) * 0.00666;

		uv += distortion;
		uv = clamp(uv, vec2(0.0, 0.0), vec2(1.0, 1.0));

		// no gamma or intensity here, it has been applied before
		// (this is just for postprocessing)
		vec4 res = texture(u_FboSampler0, uv);
		// apply the v_blend, usually blended as a colored quad with:
		// glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		res.rgb = v_blend.a * v_blend.rgb + (1.0 - v_blend.a)*res.rgb;
		outColor =  res;
	}
);