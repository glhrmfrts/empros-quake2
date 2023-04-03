static const char* fragmentSrc3Dsprite = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		void main()
		{
			vec4 texel = texture(tex, passTexCoord);

			// apply gamma correction and intensity
			texel.rgb *= intensity;
			outColor = texel;

			float fogDensity = fogParams.w/64.0;
			float fog = exp(-fogDensity * fogDensity * passFogCoord * passFogCoord);
			fog = clamp(fog, 0.0, 1.0);
			outColor.rgb = mix(fogParams.xyz, outColor.rgb, fog);

			//outColor.rgb = pow(outColor.rgb, vec3(gamma));
			outColor.a = texel.a*alpha; // I think alpha shouldn't be modified by gamma and intensity
		}
);

static const char* fragmentSrc3DspriteAlpha = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		void main()
		{
			vec4 texel = texture(tex, passTexCoord);

			if(texel.a <= 0.666)
				discard;

			// apply gamma correction and intensity
			texel.rgb *= intensity;

			outColor = texel;

			float fogDensity = fogParams.w/64.0;
			float fog = exp(-fogDensity * fogDensity * passFogCoord * passFogCoord);
			fog = clamp(fog, 0.0, 1.0);
			outColor.rgb = mix(fogParams.xyz, outColor.rgb, fog);

			//outColor.rgb = pow(outColor.rgb, vec3(gamma));
			outColor.a = texel.a*alpha; // I think alpha shouldn't be modified by gamma and intensity
		}
);