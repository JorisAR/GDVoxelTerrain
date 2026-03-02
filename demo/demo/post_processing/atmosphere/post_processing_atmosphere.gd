@tool
extends CompositorEffect
class_name PostProcessingAtmosphere

const MATRICES_BUFFER_SIZE := 128
const PROPERTIES_BUFFER_SIZE := 32

var rd: RenderingDevice
var shader: RID
var pipeline: RID
var depth_sampler: RID
var matrices_buffer_rid: RID
var properties_buffer_rid: RID
var uniform_set_1: RID

@export_tool_button("Reload Shader") var reload_shader_action = _init

@export var position: Vector3
@export var radius: float
@export var color: Color = Color(0.4, 0.6, 0.9, 1.0)

func _init() -> void:
	effect_callback_type = EFFECT_CALLBACK_TYPE_POST_TRANSPARENT
	rd = RenderingServer.get_rendering_device()
	RenderingServer.call_on_render_thread(Callable(self , "_initialize_compute"))

# System notifications, we want to react on the notification that
# alerts us we are about to be destroyed.
func _notification(what: int) -> void:
	if what != NOTIFICATION_PREDELETE or not rd:
		return

	var local_rd := rd
	var local_uniform_set_1 := uniform_set_1
	var local_depth_sampler := depth_sampler
	var local_matrices_buffer_rid := matrices_buffer_rid
	var local_properties_buffer_rid := properties_buffer_rid
	var local_pipeline := pipeline
	var local_shader := shader

	uniform_set_1 = RID()
	depth_sampler = RID()
	matrices_buffer_rid = RID()
	properties_buffer_rid = RID()
	pipeline = RID()
	shader = RID()

	RenderingServer.call_on_render_thread(func() -> void:
		if local_uniform_set_1.is_valid():
			local_rd.free_rid(local_uniform_set_1)
		if local_depth_sampler.is_valid():
			local_rd.free_rid(local_depth_sampler)
		if local_matrices_buffer_rid.is_valid():
			local_rd.free_rid(local_matrices_buffer_rid)
		if local_properties_buffer_rid.is_valid():
			local_rd.free_rid(local_properties_buffer_rid)
		if local_pipeline.is_valid():
			local_rd.free_rid(local_pipeline)
		if local_shader.is_valid():
			local_rd.free_rid(local_shader)
	)


#region Code in this region runs on the rendering thread.
# Compile our shader at initialization.
func _initialize_compute() -> void:
	_free_render_resources()
	
	rd = RenderingServer.get_rendering_device()
	if not rd:
		return
		
	# Compile our shader.
	var shader_file := load("res://demo/post_processing/atmosphere/post_processing_atmosphere.glsl")
	var shader_spirv: RDShaderSPIRV = shader_file.get_spirv()

	shader = rd.shader_create_from_spirv(shader_spirv)
	if shader.is_valid():
		pipeline = rd.compute_pipeline_create(shader)
		_initialize_uniform_resources()


func _free_render_resources() -> void:
	if not rd:
		return

	if uniform_set_1.is_valid():
		rd.free_rid(uniform_set_1)
		uniform_set_1 = RID()
	if depth_sampler.is_valid():
		rd.free_rid(depth_sampler)
		depth_sampler = RID()
	if matrices_buffer_rid.is_valid():
		rd.free_rid(matrices_buffer_rid)
		matrices_buffer_rid = RID()
	if properties_buffer_rid.is_valid():
		rd.free_rid(properties_buffer_rid)
		properties_buffer_rid = RID()
	if pipeline.is_valid():
		rd.free_rid(pipeline)
		pipeline = RID()
	if shader.is_valid():
		rd.free_rid(shader)
		shader = RID()


func _initialize_uniform_resources() -> void:
	if not rd or not shader.is_valid():
		return

	if not depth_sampler.is_valid():
		depth_sampler = rd.sampler_create(RDSamplerState.new())

	if not matrices_buffer_rid.is_valid():
		var zero_matrices := PackedByteArray()
		zero_matrices.resize(MATRICES_BUFFER_SIZE)
		matrices_buffer_rid = rd.uniform_buffer_create(MATRICES_BUFFER_SIZE, zero_matrices)

	if not properties_buffer_rid.is_valid():
		var zero_properties := PackedByteArray()
		zero_properties.resize(PROPERTIES_BUFFER_SIZE)
		properties_buffer_rid = rd.uniform_buffer_create(PROPERTIES_BUFFER_SIZE, zero_properties)

	uniform_set_1 = RID()
	if matrices_buffer_rid.is_valid() and properties_buffer_rid.is_valid():
		var matrices_uniform: RDUniform = RDUniform.new()
		matrices_uniform.uniform_type = RenderingDevice.UNIFORM_TYPE_UNIFORM_BUFFER
		matrices_uniform.binding = 0
		matrices_uniform.add_id(matrices_buffer_rid)

		var properties_uniform: RDUniform = RDUniform.new()
		properties_uniform.uniform_type = RenderingDevice.UNIFORM_TYPE_UNIFORM_BUFFER
		properties_uniform.binding = 1
		properties_uniform.add_id(properties_buffer_rid)

		uniform_set_1 = rd.uniform_set_create([matrices_uniform, properties_uniform], shader, 1)


func _should_render_atmosphere(camera_position: Vector3, camera_forward: Vector3) -> bool:
	if radius <= 0.0:
		return false

	var to_center := position - camera_position
	var abs_radius := absf(radius)
	if to_center.length_squared() <= abs_radius * abs_radius:
		return true

	# Conservative culling: when the entire sphere is strictly behind the camera.
	return to_center.dot(camera_forward) > -abs_radius


# Called by the rendering thread every frame.
func _render_callback(p_effect_callback_type: EffectCallbackType, p_render_data: RenderData) -> void:
	if rd and p_effect_callback_type == EFFECT_CALLBACK_TYPE_POST_TRANSPARENT and pipeline.is_valid():
		_initialize_uniform_resources()
		if not depth_sampler.is_valid() or not uniform_set_1.is_valid():
			return

		# Get our render scene buffers object, this gives us access to our render buffers.
		# Note that implementation differs per renderer hence the need for the cast.
		var render_scene_buffers := p_render_data.get_render_scene_buffers()
		var render_scene_data = p_render_data.get_render_scene_data()
		if render_scene_buffers:
			# Get our render size, this is the 3D render resolution!
			var size: Vector2i = render_scene_buffers.get_internal_size()
			if size.x == 0 and size.y == 0:
				return

			# We can use a compute shader here.
			@warning_ignore("integer_division")
			var x_groups := (size.x - 1) / 8 + 1
			@warning_ignore("integer_division")
			var y_groups := (size.y - 1) / 8 + 1
			var z_groups := 1

			# Create push constant.
			# Must be aligned to 16 bytes and be in the same order as defined in the shader.
			
			var camera_transform := render_scene_data.get_cam_transform()
			var camera_position := camera_transform.origin
			var camera_forward := -camera_transform.basis.z
			if not _should_render_atmosphere(camera_position, camera_forward):
				return

			var push_constant := PackedFloat32Array([
				size.x, size.y, 0, 0,
				camera_position.x, camera_position.y, camera_position.z, 0
			])
			
			var inv_proj := render_scene_data.get_cam_projection().inverse()
			var inv_proj_mat = [
			  inv_proj.x.x, inv_proj.x.y, inv_proj.x.z, inv_proj.x.w,
			  inv_proj.y.x, inv_proj.y.y, inv_proj.y.z, inv_proj.y.w,
			  inv_proj.z.x, inv_proj.z.y, inv_proj.z.z, inv_proj.z.w,
			  inv_proj.w.x, inv_proj.w.y, inv_proj.w.z, inv_proj.w.w,
			]
			
			# inv(P * V) = inv(V) * inv(P)
			var inv_view_proj := Projection(camera_transform) * inv_proj
			var inv_view_proj_mat = [
			  inv_view_proj.x.x, inv_view_proj.x.y, inv_view_proj.x.z, inv_view_proj.x.w,
			  inv_view_proj.y.x, inv_view_proj.y.y, inv_view_proj.y.z, inv_view_proj.y.w,
			  inv_view_proj.z.x, inv_view_proj.z.y, inv_view_proj.z.z, inv_view_proj.z.w,
			  inv_view_proj.w.x, inv_view_proj.w.y, inv_view_proj.w.z, inv_view_proj.w.w,
			]
			
			var matrices_floats := PackedFloat32Array(inv_proj_mat)
			matrices_floats.append_array(PackedFloat32Array(inv_view_proj_mat))
			var matrices_buffer := matrices_floats.to_byte_array()
			rd.buffer_update(matrices_buffer_rid, 0, MATRICES_BUFFER_SIZE, matrices_buffer)
			
			##properties
			var properties := [
				color.r, color.g, color.b, color.a,
				position.x, position.y, position.z, radius
			]
			var properties_buffer := PackedFloat32Array(properties).to_byte_array()
			rd.buffer_update(properties_buffer_rid, 0, PROPERTIES_BUFFER_SIZE, properties_buffer)

			# Loop through views just in case we're doing stereo rendering. No extra cost if this is mono.
			var view_count: int = render_scene_buffers.get_view_count()
			for view in view_count:
				# Get the RID for our color image, we will be reading from and writing to it.
				var color_image: RID = render_scene_buffers.get_color_layer(view)
				var depth_image: RID = render_scene_buffers.get_depth_layer(view)

				## set0 : textures
				var uniform_color_image := RDUniform.new()
				uniform_color_image.uniform_type = RenderingDevice.UNIFORM_TYPE_IMAGE
				uniform_color_image.binding = 0
				uniform_color_image.add_id(color_image)

				var uniform_depth_image := RDUniform.new()
				uniform_depth_image.uniform_type = RenderingDevice.UNIFORM_TYPE_SAMPLER_WITH_TEXTURE
				uniform_depth_image.binding = 1
				uniform_depth_image.add_id(depth_sampler)
				uniform_depth_image.add_id(depth_image)
				var uniform_set_0 := UniformSetCacheRD.get_cache(shader, 0, [uniform_color_image, uniform_depth_image])

				# Run our compute shader.
				var compute_list := rd.compute_list_begin()
				rd.compute_list_bind_compute_pipeline(compute_list, pipeline)
				rd.compute_list_bind_uniform_set(compute_list, uniform_set_0, 0)
				rd.compute_list_bind_uniform_set(compute_list, uniform_set_1, 1)
				rd.compute_list_set_push_constant(compute_list, push_constant.to_byte_array(), push_constant.size() * 4)
				rd.compute_list_dispatch(compute_list, x_groups, y_groups, z_groups)
				rd.compute_list_end()
#endregion
