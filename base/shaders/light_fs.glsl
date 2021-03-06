/**
 * @file light_fs.glsl
 * @brief Dynamic lighting fragment shader.
 */

#ifndef glsl110
	/** Linkage into a shader from a previous stage, variable is copied in.*/
        #define in_qualifier in
#else
        /** Deprecated after glsl110; linkage between a vertex shader and a fragment shader for interpolated data.*/
        #define in_qualifier varying
#endif

in_qualifier vec3 point;
in_qualifier vec3 normal;

vec3 LightContribution(in gl_LightSourceParameters lightSource, in vec4 diffuse, in vec3 lightmap){

	vec3 light = vec3(0.0);

	if(lightSource.constantAttenuation > 0.0){
		vec3 delta = lightSource.position.xyz - point;
		float dist = length(delta);
		if (lightSource.position.w == 0.0){
			vec3 dir = normalize(delta);
			float d = max(0.0, dot(normal, dir));
			light += lightSource.diffuse.rgb * d;
			light += lightSource.ambient.rgb;
		} else if(dist < lightSource.constantAttenuation){
			vec3 dir = normalize(delta);
			float d = max(0.0, dot(normal, dir));
			if(d > 0.0){
				float distAttenuation = 1.0 - dist / lightSource.constantAttenuation;
				float linearAttenuation = 1.8 * distAttenuation;
				float quadraticAttenuation = 3.5 * distAttenuation * distAttenuation;
				float attenuation = 0.15 + linearAttenuation + quadraticAttenuation;
				/** @todo the next line is the reason it does not compile on my ati x600 */
				light += lightSource.diffuse.rgb * d * attenuation;
				light += lightSource.ambient.rgb;
			}
		}
	}

	return light;
}

/**
 * @brief LightFragment.
 */
vec4 LightFragment(in vec4 diffuse, in vec3 lightmap){

	vec3 light = vec3(0.0);

#unroll r_dynamic_lights
	light += LightContribution(gl_LightSource[$], diffuse, lightmap);
#endunroll

	light = clamp(light, 0.0, 1.8);

	/* Now modulate the diffuse sample with the modified lightmap.*/
	vec4 lightColor;
	lightColor.rgb = diffuse.rgb * (lightmap + light);
	/* Lastly modulate the alpha channel by the color.*/
	lightColor.a = diffuse.a * gl_Color.a;

	return lightColor;
}
