#[compute]
#version 450

// Invocations in the (x, y, z) dimension
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(rgba16f, set = 0, binding = 0) uniform image2D color_image;
layout(set = 0, binding = 1) uniform sampler2D depth_image;

// Our push constant
layout(push_constant, std430) uniform Params {
	vec2 raster_size;
	vec2 padding;
	vec3 camera_position;
	float padding2;
} params;

layout(set=1, binding = 0) uniform Matrices {
	mat4 inv_projection;
	mat4 inv_view_projection;
} matrices;

layout(set=1, binding = 1) uniform Properties {
	vec4 color;
	vec3 position;
    float radius;
} properties;


bool intersectSphere(vec3 ray_origin, vec3 ray_dir, vec3 sphere_center, float sphere_radius, out vec2 t) {
    vec3 oc = ray_origin - sphere_center;
    float a = dot(ray_dir, ray_dir);
    float b = 2.0 * dot(oc, ray_dir);
    float c = dot(oc, oc) - sphere_radius * sphere_radius;
    float discriminant = b * b - 4.0 * a * c;

    if (discriminant < 0.0) {
        return false; // No intersection
    }

    float sqrtD = sqrt(discriminant);
    float t1 = (-b - sqrtD) / (2.0 * a);
    float t2 = (-b + sqrtD) / (2.0 * a);

	if (t2 < 0.0 || t1 > t2) {
        return false; // No intersection
    }

    // If the ray starts inside the sphere, first hit point is at t=0
    if (t1 < 0.0 && t2 >= 0.0) {
        t.x = 0.0;
        t.y = t2;
    } else {
        t.x = t1;
        t.y = t2;
    }

    return true;
}


// The code we want to execute in each invocation
void main() {
	ivec2 uv = ivec2(gl_GlobalInvocationID.xy);
	ivec2 size = ivec2(params.raster_size);
	vec2 screen_uv = vec2(uv + 0.5) / size;

	// Prevent reading/writing out of bounds.
	if (uv.x >= size.x || uv.y >= size.y) {
		return;
	}

	// Read from our color buffer.
	vec4 original_color = imageLoad(color_image, uv);
	float depth = texture(depth_image, screen_uv).r;
	vec4 ndc = vec4(screen_uv * 2.0 - 1.0, depth, 1.0);
	vec4 view = matrices.inv_projection * ndc;
  	view.xyz /= view.w;
  	float linear_depth = -view.z;

	// Compute world-space ray direction
    vec4 world_pos = matrices.inv_view_projection * vec4(screen_uv * 2.0 - 1.0, 0.0, 1.0);
    world_pos /= world_pos.w;
    vec3 ray_origin = params.camera_position;
    vec3 ray_dir = normalize(world_pos.xyz - ray_origin);

    // Sphere properties

	vec2 t;
    if (intersectSphere(ray_origin, ray_dir, properties.position, properties.radius, t)) {
		float hit_point = min(t.y, linear_depth);
		float strength = (hit_point - t.x) / (properties.radius * 2.0f);
		if(strength < 0.0f) {
			strength = 0.0f;
		}
		strength = pow(strength, 1.2f);
        // vec4 atmosphere_color = vec4(0.4, 0.6, 0.9, 1.0f);
        // vec4 atmosphere_color = vec4(0.4, 0.6, 0.9, 1.0f);
		vec4 color = mix(original_color, properties.color, strength);
        imageStore(color_image, uv, color);
    }
	// imageStore(color_image, uv, original_color);
}
