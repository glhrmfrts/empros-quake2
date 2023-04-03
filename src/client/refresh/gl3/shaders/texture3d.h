static const char* fragmentSrc3D = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		void main()
		{
			vec4 texel = texture(tex, passTexCoord);

			// apply intensity and gamma
			texel.rgb *= intensity;

			outColor.rgb = texel.rgb;

			float fogDensity = fogParams.w/64.0;
			float fog = exp(-fogDensity * fogDensity * passFogCoord * passFogCoord);
			fog = clamp(fog, 0.0, 1.0);
			outColor.rgb = mix(fogParams.xyz, outColor.rgb, fog);

			//outColor.rgb = pow(outColor.rgb, vec3(gamma));
			outColor.a = texel.a*alpha; // I think alpha shouldn't be modified by gamma and intensity
		}
);