shader_type spatial;
// Octahedral impostor: a camera-facing quad showing an object from pre-baked
// views (frames in an atlas, one per direction on an octahedron or, with
// `isFullSphere` off, a hemi-octahedron above the object). Each pixel blends
// the three frames nearest to the view direction; each frame is looked up
// where the view ray crosses that frame's plane, so frames line up while the
// view turns. Lighting uses a rounded normal (facing the camera, bending
// outwards towards the silhouette, a little up), like a canopy: the baked
// normal maps cannot be relied on (the bakes here hold no normals).
//
// The uniform names match the materials of the Octahedral Impostors baker for
// Godot, so its baked QuadMeshes can use this shader as they are. As a
// JarTerrainFeature LOD (a MeshInstance3D in the feature scene with a
// visibility range) it switches per instance (jar_population.gdshaderinc).
render_mode cull_disabled, depth_draw_opaque;

#include "res://addons/jar_voxel_terrain/shaders/jar_population.gdshaderinc"

uniform vec4 albedo : source_color = vec4(1.0);
uniform float specular : hint_range(0.0, 1.0) = 0.5;
uniform float metallic : hint_range(0.0, 1.0) = 0.0; // baked materials often say 1: set 0 for plants
uniform float roughness : hint_range(0.0, 1.0) = 1.0;
uniform vec2 imposterFrames = vec2(8.0, 8.0);
uniform vec3 positionOffset = vec3(0.0); // object center, object space
uniform bool isFullSphere = false;
uniform float alpha_clamp : hint_range(0.0, 1.0) = 0.5;
uniform bool dither = false;
uniform float scale = 1.0; // half the frame size, object space
// Unused here; kept so baked materials load without warnings.
uniform float depth_scale = 0.0;
uniform float normalmap_depth = 1.0;
uniform float aabb_max = 1.0;
uniform sampler2D imposterTextureAlbedo : source_color, filter_linear_mipmap;
uniform sampler2D imposterTextureNormal : hint_normal, filter_linear_mipmap;
uniform sampler2D imposterTextureDepth : filter_linear_mipmap;
uniform sampler2D imposterTextureOrm : filter_linear_mipmap;

varying vec2 frame0;
varying vec2 frame1;
varying vec2 frame2;
varying vec3 weights;
varying vec2 uv0;
varying vec2 uv1;
varying vec2 uv2;
varying vec3 quad_right; // object space
varying vec3 quad_down;
varying vec3 quad_out;

// Direction -> position on the (hemi-)octahedron grid, in [0, 1]^2.
vec2 direction_to_grid(vec3 d) {
	if (isFullSphere) {
		vec3 octant = sign(d);
		vec3 o = d / dot(d, octant);
		if (o.y < 0.0) {
			vec3 a = abs(o);
			o.xz = octant.xz * vec2(1.0 - a.z, 1.0 - a.x);
		}
		return o.xz * 0.5 + 0.5;
	}
	d.y = max(d.y, 0.001);
	d = normalize(d);
	vec3 o = d / (abs(d.x) + abs(d.y) + abs(d.z));
	return vec2(o.x + o.z, o.z - o.x) * 0.5 + 0.5;
}

// Grid position in [0, 1]^2 -> direction.
vec3 grid_to_direction(vec2 g) {
	vec3 p;
	if (isFullSphere) {
		g = g * 2.0 - 1.0;
		p = vec3(g.x, 0.0, g.y);
		vec2 a = abs(p.xz);
		p.y = 1.0 - a.x - a.y;
		if (p.y < 0.0)
			p.xz = sign(p.xz) * vec2(1.0 - a.y, 1.0 - a.x);
	} else {
		p = vec3(g.x - g.y, 0.0, -1.0 + g.x + g.y);
		vec2 a = abs(p.xz);
		p.y = 1.0 - a.x - a.y;
	}
	return normalize(p);
}

// The baking camera's right and down axes for a view from direction `z`.
void frame_axes(vec3 z, out vec3 x, out vec3 y) {
	vec3 up = abs(z.y) > 0.999 ? vec3(0.0, 0.0, -1.0) : vec3(0.0, 1.0, 0.0);
	x = normalize(cross(up, z));
	y = normalize(cross(x, z));
}

// Where the ray from `eye` through `point` crosses the plane of the frame seen
// from `z` (through the center), in that frame's [0, 1] coordinates.
vec2 frame_uv(vec3 z, vec3 eye, vec3 point) {
	vec3 x, y;
	frame_axes(z, x, y);
	vec3 ray = point - eye;
	float t = dot(positionOffset - eye, z) / min(dot(ray, z), -1e-4);
	vec3 hit = eye + ray * t - positionOffset;
	return vec2(dot(hit, x), dot(hit, y)) / (2.0 * scale) + 0.5;
}

void vertex() {
	vec3 eye = (inverse(MODEL_MATRIX) * vec4(CAMERA_POSITION_WORLD, 1.0)).xyz;
	vec3 to_eye = normalize(eye - positionOffset);
	// The quad, facing the camera.
	vec3 x, y;
	frame_axes(to_eye, x, y);
	vec2 corner = (UV - 0.5) * 2.0 * scale;
	vec3 point = positionOffset + x * corner.x + y * corner.y;
	VERTEX = point;
	quad_right = x;
	quad_down = y;
	quad_out = to_eye;

	// The three frames around the view direction, with blend weights.
	vec2 last = imposterFrames - 1.0;
	vec2 grid = clamp(direction_to_grid(to_eye) * last, vec2(0.0), last);
	vec2 base = min(floor(grid), last - 1.0);
	vec2 f = grid - base;
	bool upper = f.x > f.y;
	frame0 = base;
	frame1 = base + (upper ? vec2(1.0, 0.0) : vec2(0.0, 1.0));
	frame2 = base + 1.0;
	weights = vec3(min(1.0 - f.x, 1.0 - f.y), abs(f.x - f.y), min(f.x, f.y));
	uv0 = frame_uv(grid_to_direction(frame0 / last), eye, point);
	uv1 = frame_uv(grid_to_direction(frame1 / last), eye, point);
	uv2 = frame_uv(grid_to_direction(frame2 / last), eye, point);
	JAR_POPULATION_VERTEX
}

vec4 sample_frame(sampler2D atlas, vec2 frame, vec2 uv) {
	return texture(atlas, (frame + clamp(uv, 0.0, 1.0)) / imposterFrames);
}

void fragment() {
	vec4 a0 = sample_frame(imposterTextureAlbedo, frame0, uv0);
	vec4 a1 = sample_frame(imposterTextureAlbedo, frame1, uv1);
	vec4 a2 = sample_frame(imposterTextureAlbedo, frame2, uv2);
	vec4 color = a0 * weights.x + a1 * weights.y + a2 * weights.z;
	if (color.a < alpha_clamp)
		discard;
	vec2 c = (UV - 0.5) * 2.0;
	vec3 n = normalize(quad_right * c.x + quad_down * c.y + quad_out * 0.8 + vec3(0.0, 0.4, 0.0));
	NORMAL = normalize((VIEW_MATRIX * (MODEL_MATRIX * vec4(n, 0.0))).xyz);
	ALBEDO = color.rgb * albedo.rgb;
	SPECULAR = specular;
	METALLIC = metallic;
	ROUGHNESS = roughness;
}
