@tool
extends CompositorEffect
class_name PostProcessingAtmosphere

var rd: RenderingDevice
var shader: RID
var pipeline: RID

@export_tool_button("Reload Shader") var reload_shader_action = _init

@export var position : Vector3
@export var radius : float
@export var color : Color = Color(0.4, 0.6, 0.9, 1.0)

func _init() -> void:
	effect_callback_type = EFFECT_CALLBACK_TYPE_POST_TRANSPARENT
	rd = RenderingServer.get_rendering_device()
	RenderingServer.call_on_render_thread(_initialize_compute)

# System notifications, we want to react on the notification that
# alerts us we are about to be destroyed.
func _notification(what: int) -> void:
	if what == NOTIFICATION_PREDELETE:
		if shader.is_valid():
			# Freeing our shader will also free any dependents such as the pipeline!
			rd.free_rid(shader)


#region Code in this region runs on the rendering thread.
# Compile our shader at initialization.
func _initialize_compute() -> void:
	if shader.is_valid():
		rd.free_rid(shader)
	
	rd = RenderingServer.get_rendering_device()
	if not rd:
		return
		
	# Compile our shader.
	var shader_file := load("res://demo/post_processing/atmosphere/post_processing_atmosphere.glsl")
	var shader_spirv: RDShaderSPIRV = shader_file.get_spirv()

	shader = rd.shader_create_from_spirv(shader_spirv)
	if shader.is_valid():
		pipeline = rd.compute_pipeline_create(shader)


# Called by the rendering thread every frame.
func _render_callback(p_effect_callback_type: EffectCallbackType, p_render_data: RenderData) -> void:
	if rd and p_effect_callback_type == EFFECT_CALLBACK_TYPE_POST_TRANSPARENT and pipeline.is_valid():
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
			
			var camera_position := render_scene_data.get_cam_transform().origin;
			var push_constant := PackedFloat32Array([
				size.x, size.y, 0, 0,
				camera_position.x, camera_position.y, camera_position.z, 0
			])
			
			var inv_proj := render_scene_data.get_cam_projection().inverse();
			var inv_proj_mat= [
			  inv_proj.x.x, inv_proj.x.y, inv_proj.x.z, inv_proj.x.w,
			  inv_proj.y.x, inv_proj.y.y, inv_proj.y.z, inv_proj.y.w, 
			  inv_proj.z.x, inv_proj.z.y, inv_proj.z.z, inv_proj.z.w,
			  inv_proj.w.x, inv_proj.w.y, inv_proj.w.z, inv_proj.w.w,
			]
			
			## compute VP: Projection * InverseCameraTransform -> then invert
			var inv_view_proj := (render_scene_data.get_cam_projection() 
				* Projection(render_scene_data.get_cam_transform().affine_inverse())).inverse()
			var inv_view_proj_mat= [
			  inv_view_proj.x.x, inv_view_proj.x.y, inv_view_proj.x.z, inv_view_proj.x.w,
			  inv_view_proj.y.x, inv_view_proj.y.y, inv_view_proj.y.z, inv_view_proj.y.w, 
			  inv_view_proj.z.x, inv_view_proj.z.y, inv_view_proj.z.z, inv_view_proj.z.w,
			  inv_view_proj.w.x, inv_view_proj.w.y, inv_view_proj.w.z, inv_view_proj.w.w,
			]
			
			var ip_buffer = PackedFloat32Array(inv_proj_mat).to_byte_array()
			var ivp_buffer = PackedFloat32Array(inv_view_proj_mat).to_byte_array()
			var matrices_buffer = PackedByteArray()
			matrices_buffer.append_array(ip_buffer)
			matrices_buffer.append_array(ivp_buffer)
			
			##properties
			var properties = [
				color.r, color.g, color.b, color.a,
				position.x, position.y, position.z, radius
			]
			var properties_buffer = PackedFloat32Array(properties).to_byte_array()

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
				var depth_sampler : RID = rd.sampler_create(RDSamplerState.new())
				uniform_depth_image.uniform_type = RenderingDevice.UNIFORM_TYPE_SAMPLER_WITH_TEXTURE
				uniform_depth_image.binding = 1
				uniform_depth_image.add_id(depth_sampler)
				uniform_depth_image.add_id(depth_image)				
				var uniform_set_0 := UniformSetCacheRD.get_cache(shader, 0, [uniform_color_image, uniform_depth_image])
				
				## set1: matrices / properties
				var mat_buffer : RID =  rd.uniform_buffer_create(128, matrices_buffer)
				var matrices_uniform : RDUniform = RDUniform.new()
				matrices_uniform.uniform_type = RenderingDevice.UNIFORM_TYPE_UNIFORM_BUFFER
				matrices_uniform.binding = 0
				matrices_uniform.add_id(mat_buffer)
				
				var properties_buffer_rid : RID =  rd.uniform_buffer_create(properties_buffer.size(), properties_buffer)
				var properties_uniform : RDUniform = RDUniform.new()
				properties_uniform.uniform_type = RenderingDevice.UNIFORM_TYPE_UNIFORM_BUFFER
				properties_uniform.binding = 1
				properties_uniform.add_id(properties_buffer_rid)
				
				var uniform_set_1: RID = UniformSetCacheRD.get_cache(shader, 1, [  matrices_uniform, properties_uniform ])

				# Run our compute shader.
				var compute_list := rd.compute_list_begin()
				rd.compute_list_bind_compute_pipeline(compute_list, pipeline)
				rd.compute_list_bind_uniform_set(compute_list, uniform_set_0, 0)
				rd.compute_list_bind_uniform_set(compute_list, uniform_set_1, 1)
				rd.compute_list_set_push_constant(compute_list, push_constant.to_byte_array(), push_constant.size() * 4)
				rd.compute_list_dispatch(compute_list, x_groups, y_groups, z_groups)
				rd.compute_list_end()
#endregion
