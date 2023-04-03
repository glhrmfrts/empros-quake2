//
// Shadow maps
//

static const char* vertexSrcShadowMap = MULTILINE_STRING(
	// it gets attributes and uniforms from vertexCommon3D

	out vec3 passWorldCoord;
	out vec3 passNormal;

	void main()
	{
		passTexCoord = texCoord;
		vec4 worldCoord = transModel * vec4(position, 1.0);
		passWorldCoord = worldCoord.xyz;
		vec4 worldNormal = transModel * vec4(normal, 0.0f);
		passNormal = normalize(worldNormal.xyz);

		gl_Position = transProj * transView * worldCoord;

		passFogCoord = gl_Position.w;
	}
);

static const char* fragmentSrcShadowMap = MULTILINE_STRING(

	void main()
	{
		vec3 color = abs(fogParams.rgb);
		if (fogParams.r < 0.0)
		{
			color = vec3(1.0, 1.0, 0.0);
		}
		else if (fogParams.g < 0.0)
		{
			color = vec3(0.0, 1.0, 1.0);
		}
		else if (fogParams.b < 0.0)
		{
			color = vec3(1.0);
		}
		outColor = texture(tex, passTexCoord);
		gl_FragDepth = gl_FragCoord.z;
	}
);