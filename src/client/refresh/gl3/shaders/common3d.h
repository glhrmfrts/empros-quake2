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
