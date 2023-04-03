static const char* fragmentSrc3Dsky = MULTILINE_STRING(

		// it gets attributes and uniforms from fragmentCommon3D

		void main()
		{
			vec4 texel = texture(tex, passTexCoord);

			// TODO: something about GL_BLEND vs GL_ALPHATEST etc

			// apply gamma correction
			// texel.rgb *= intensity; // TODO: really no intensity for sky?

			outColor = texel * (1.0f+emission);
			//outColor.rgb = pow(outColor.rgb, vec3(gamma));

			float fogDensity = fogParams.w/64.0;
			float fog = exp(-fogDensity * fogDensity * passFogCoord * passFogCoord);
			fog = clamp(fog, 0.0, 1.0);
			outColor.rgb = mix(fogParams.xyz, outColor.rgb, fog);

			outColor.a = texel.a*alpha; // I think alpha shouldn't be modified by gamma and intensity
		}
);