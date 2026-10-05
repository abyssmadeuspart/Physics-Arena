package common

import "core:mem"
import "core:os"
import "core:math"

Ray_Shape :: enum u32
{
	Box, Sphere, Capsule, Hull, Mesh,
}
Ray_Phase :: enum u32
{
	Primary, Shuffled, Shadow, Reflection, Ambient, Filtered, Collider_Hits, Mesh_Hits, Updated,
}
Ray_Api :: enum u32
{
	Ordinary, Native_Batch,
}
Ray_Status :: enum u32
{
	Ok, Invalid, Capacity, Io, Unsupported, Failed,
}
Ray_Pose :: struct
{
	position: Vector3, rotation: Quaternion,
}
Ray_Collider :: struct
{
	shape: Ray_Shape,
	category: u32,
	moving: u32,
	hull: u32,
	pose: Ray_Pose,
	size: Vector3,
	first_triangle: u32,
	triangle_count: u32,
}
Ray_Triangle :: struct
{
	a, b, c: Vector3,
}
Ray_Hull :: struct
{
	first_vertex, vertex_count: u32,
}
Ray_Input :: struct
{
	origin, translation: Vector3, length: f32, mask, pixel: u32,
}
Ray_Hit :: struct
{
	distance: f64, normal: Vector3, collider, source, flags: u32,
}
Ray_Range :: struct
{
	first, count: u32,
}
Ray_Output :: struct
{
	first, count, written: u32, status: Ray_Status,
}
Ray_Scene :: struct
{
	colliders: []Ray_Collider,
	triangles: []Ray_Triangle,
	hulls: []Ray_Hull,
	vertices: []Vector3,
	cameras: [6]Ray_Pose,
	light: Vector3,
}
Ray_Corpus_Phase :: struct
{
	view: u32,
	phase: Ray_Phase,
	width, height, native_event_capacity: u32,
	rays: []Ray_Input,
	expected: []Ray_Range,
	hits: []Ray_Hit,
	ray_storage: []Ray_Input,
	range_storage: []Ray_Range,
	hit_storage: []Ray_Hit,
}
#assert(size_of(Ray_Collider) == 64 && size_of(Ray_Input) == 36 && size_of(Ray_Hit) == 32)
#assert(size_of(Ray_Output) == 16 && size_of(Ray_Pose) == 28)
RAY_ACTIVE_BYTE_LIMIT :: 2 * 1024 * 1024 * 1024

ray_phase_name :: proc(phase: Ray_Phase) -> string
{
	names: [9]string = {"primary-coherent", "primary-shuffled", "shadows", "reflections", "ambient-occlusion",
		"filtered-closest", "collider_hits", "mesh_surface_hits", "updated-primary"}
	return names[u32(phase)]
}

ray_updated_pose :: proc(collider: ^Ray_Collider, view: u32) -> Ray_Pose
{
	pose: Ray_Pose = collider.pose
	if collider.moving != 0
	{
		pose.position.x += 0.17 * f32(1 + view)
		pose.position.y += 0.11 * f32(view % 3 + 1)
		pose.position.z -= 0.13 * f32(view % 2 + 1)
	}
	return pose
}

ray_read_array :: proc(file: ^os.File, count: u32, remaining: ^u64, output: ^[]$T) -> Status
{
	bytes: u64 = u64(count) * u64(size_of(T))
	if bytes > remaining^ || bytes > RAY_ACTIVE_BYTE_LIMIT
	{
		return .Invalid
	}
	allocation: mem.Allocator_Error
	output^, allocation = make([]T, int(count))
	if allocation != nil
	{
		return .Capacity
	}
	read: int
	error: os.Error
	read, error = os.read_full(file, mem.slice_to_bytes(output^))
	if error != nil || read != int(bytes)
	{
		return .Io_Failed
	}
	remaining^ -= bytes
	return .Ok
}

ray_scene_destroy :: proc(scene: ^Ray_Scene)
{
	delete(scene.colliders)
	delete(scene.triangles)
	delete(scene.hulls)
	delete(scene.vertices)
	scene^ = {}
}

ray_finite_vector :: proc(value: Vector3) -> u32
{
	return u32(!math.is_nan(value.x) && !math.is_inf(value.x) && !math.is_nan(value.y) && !math.is_inf(value.y) && !math.is_nan(value.z) && !math.is_inf(value.z))
}

ray_valid_pose :: proc(pose: Ray_Pose) -> u32
{
	q: Quaternion = pose.rotation
	norm: f64 = f64(q.x)*f64(q.x) + f64(q.y)*f64(q.y) + f64(q.z)*f64(q.z) + f64(q.w)*f64(q.w)
	return u32(ray_finite_vector(pose.position) != 0 && !math.is_nan(norm) && !math.is_inf(norm) && math.abs(norm-1) < 1e-5)
}

ray_scene_read :: proc(path: string, scene: ^Ray_Scene) -> Status
{
	file: ^os.File
	error: os.Error
	file, error = os.open(path, {.Read})
	if error != nil
	{
		return .Io_Failed
	}
	defer os.close(file)
	size: i64
	size, error = os.file_size(file)
	if error != nil || size < 204 || size > RAY_ACTIVE_BYTE_LIMIT
	{
		return .Invalid
	}
	header: [6]u32
	read: int
	read, error = os.read_full(file, mem.slice_to_bytes(header[:]))
	if error != nil || read != 24 || header[0] != 0x43545242 || header[1] != 1 || header[2] > 66560 ||
		header[3] > 1048576 || header[4] > 3 || header[5] > 96
	{
		return .Invalid
	}
	remaining: u64 = u64(size - 204)
	if ray_read_array(file, header[2], &remaining, &scene.colliders) != .Ok ||
		ray_read_array(file, header[3], &remaining, &scene.triangles) != .Ok ||
		ray_read_array(file, header[4], &remaining, &scene.hulls) != .Ok ||
		ray_read_array(file, header[5], &remaining, &scene.vertices) != .Ok || remaining != 0
	{
		return .Invalid
	}
	read, error = os.read_full(file, mem.slice_to_bytes(scene.cameras[:]))
	if error != nil || read != 168
	{
		return .Io_Failed
	}
	light: [1]Vector3
	read, error = os.read_full(file, mem.slice_to_bytes(light[:]))
	if error != nil || read != 12
	{
		return .Io_Failed
	}
	scene.light = light[0]
	if ray_finite_vector(scene.light) == 0
	{
		return .Invalid
	}
	for camera in scene.cameras
	{
		if ray_valid_pose(camera) == 0
		{
			return .Invalid
		}
	}
	for hull in scene.hulls
	{
		if hull.vertex_count < 4 || hull.vertex_count > 64 || u64(hull.first_vertex)+u64(hull.vertex_count) > u64(len(scene.vertices))
		{
			return .Invalid
		}
	}
	for vertex in scene.vertices
	{
		if ray_finite_vector(vertex) == 0
		{
			return .Invalid
		}
	}
	for triangle in scene.triangles
	{
		if ray_finite_vector(triangle.a) == 0 || ray_finite_vector(triangle.b) == 0 || ray_finite_vector(triangle.c) == 0
		{
			return .Invalid
		}
	}
	for collider in scene.colliders
	{
		if u32(collider.shape) > u32(Ray_Shape.Mesh) || collider.category == 0 || collider.moving > 1 || ray_valid_pose(collider.pose) == 0 ||
			ray_finite_vector(collider.size) == 0 || (collider.shape != .Mesh && collider.size.x <= 0) ||
			((collider.shape == .Box || collider.shape == .Hull) && (collider.size.y <= 0 || collider.size.z <= 0)) ||
			(collider.shape == .Capsule && collider.size.y < 0) || (collider.shape == .Hull && int(collider.hull) >= len(scene.hulls)) ||
			u64(collider.first_triangle)+u64(collider.triangle_count) > u64(len(scene.triangles)) ||
			(collider.shape == .Mesh && (collider.triangle_count == 0 || collider.moving != 0))
		{
			return .Invalid
		}
	}
	return .Ok
}

Ray_Phase_Metadata :: struct
{
	view: u32,
	phase: Ray_Phase,
	width, height, rays, hits, native_events: u32,
}
Ray_Phase_Capacity :: struct
{
	rays, reference_hits, output_hits, batch_rays, native_events: u64,
}

ray_phase_header :: proc(file: ^os.File) -> (Ray_Phase_Metadata, Status)
{
	size: i64
	error: os.Error
	size, error = os.file_size(file)
	if error != nil || size < 40 || size > RAY_ACTIVE_BYTE_LIMIT
	{
		return {}, .Invalid
	}
	header: [10]u32
	read: int
	read, error = os.read_full(file, mem.slice_to_bytes(header[:]))
	expected: u64 = 40 + u64(header[6]) * u64(size_of(Ray_Input) + size_of(Ray_Range)) + u64(header[7]) * u64(size_of(Ray_Hit))
	if error != nil || read != 40 || header[0] != 0x52545242 || header[1] != 1 || header[2] >= 6 || header[3] > 8 ||
		header[4] == 0 || header[4] > 1920 || header[5] == 0 || header[5] > 1080 || header[6] > 2073600 ||
		header[8] == 0 || header[9] != 0 || expected != u64(size)
	{
		return {}, .Invalid
	}
	return {header[2], Ray_Phase(header[3]), header[4], header[5], header[6], header[7], header[8]}, .Ok
}

ray_phase_metadata :: proc(path: string) -> (Ray_Phase_Metadata, Status)
{
	file: ^os.File
	error: os.Error
	file, error = os.open(path, {.Read})
	if error != nil
	{
		return {}, .Io_Failed
	}
	defer os.close(file)
	return ray_phase_header(file)
}

ray_include_capacity :: proc(capacity: ^Ray_Phase_Capacity, metadata: Ray_Phase_Metadata)
{
	capacity.rays = max(capacity.rays, u64(metadata.rays))
	capacity.reference_hits = max(capacity.reference_hits, u64(metadata.hits))
	output_hits: u64 = u64(metadata.rays)
	if metadata.phase == .Collider_Hits || metadata.phase == .Mesh_Hits
	{
		output_hits = u64(metadata.hits) + 4 * u64(metadata.rays)
	}
	capacity.output_hits = max(capacity.output_hits, output_hits)
	if metadata.phase == .Primary || metadata.phase == .Shuffled || metadata.phase == .Reflection || metadata.phase == .Updated
	{
		capacity.batch_rays = max(capacity.batch_rays, u64(metadata.rays))
	}
	capacity.native_events = max(capacity.native_events, u64(metadata.native_events))
}

ray_phase_reserve :: proc(phase: ^Ray_Corpus_Phase, capacity: Ray_Phase_Capacity) -> Status
{
	if capacity.rays * u64(size_of(Ray_Input) + size_of(Ray_Range)) + capacity.reference_hits * u64(size_of(Ray_Hit)) > RAY_ACTIVE_BYTE_LIMIT
	{
		return .Capacity
	}
	allocation: mem.Allocator_Error
	phase.ray_storage, allocation = make([]Ray_Input, int(capacity.rays))
	if allocation != nil
	{
		return .Capacity
	}
	phase.range_storage, allocation = make([]Ray_Range, int(capacity.rays))
	if allocation != nil
	{
		return .Capacity
	}
	phase.hit_storage, allocation = make([]Ray_Hit, int(capacity.reference_hits))
	return .Ok if allocation == nil else .Capacity
}

ray_phase_destroy :: proc(phase: ^Ray_Corpus_Phase)
{
	delete(phase.ray_storage)
	delete(phase.range_storage)
	delete(phase.hit_storage)
	phase^ = {}
}

ray_phase_read :: proc(path: string, phase: ^Ray_Corpus_Phase) -> Status
{
	file: ^os.File
	error: os.Error
	file, error = os.open(path, {.Read})
	if error != nil
	{
		return .Io_Failed
	}
	defer os.close(file)
	metadata: Ray_Phase_Metadata
	status: Status
	metadata, status = ray_phase_header(file)
	if status != .Ok
	{
		return status
	}
	if phase.ray_storage == nil && phase.range_storage == nil && phase.hit_storage == nil
	{
		status = ray_phase_reserve(phase, {rays = u64(metadata.rays), reference_hits = u64(metadata.hits)})
		if status != .Ok
		{
			return status
		}
	}
	if u64(metadata.rays) > u64(len(phase.ray_storage)) || u64(metadata.hits) > u64(len(phase.hit_storage))
	{
		return .Capacity
	}
	phase.view = metadata.view
	phase.phase = metadata.phase
	phase.width = metadata.width
	phase.height = metadata.height
	phase.native_event_capacity = metadata.native_events
	phase.rays = phase.ray_storage[:metadata.rays]
	phase.expected = phase.range_storage[:metadata.rays]
	phase.hits = phase.hit_storage[:metadata.hits]
	parts: [3][]u8 = {mem.slice_to_bytes(phase.rays), mem.slice_to_bytes(phase.expected), mem.slice_to_bytes(phase.hits)}
	for part in parts
	{
		read: int
		read, error = os.read_full(file, part)
		if error != nil || read != len(part)
		{
			return .Io_Failed
		}
	}
	for range in phase.expected
	{
		if u64(range.first) + u64(range.count) > u64(len(phase.hits))
		{
			return .Invalid
		}
	}
	for ray in phase.rays
	{
		length: f64 = math.sqrt(f64(ray.translation.x)*f64(ray.translation.x) + f64(ray.translation.y)*f64(ray.translation.y) + f64(ray.translation.z)*f64(ray.translation.z))
		if ray_finite_vector(ray.origin) == 0 || ray_finite_vector(ray.translation) == 0 || math.abs(length-f64(ray.length)) > 1e-5*max(1, length) || ray.length <= 0 || (math.is_nan(ray.length) || math.is_inf(ray.length)) || ray.pixel >= phase.width * phase.height || ray.mask == 0
		{
			return .Invalid
		}
	}
	for hit in phase.hits
	{
		if math.is_nan(hit.distance) || math.is_inf(hit.distance) || hit.distance < 0 ||
			ray_finite_vector(hit.normal) == 0 || hit.collider == 0 || hit.collider > 66560
		{
			return .Invalid
		}
	}
	return .Ok
}

Ray_Validation :: struct
{
	errors, hit_rays, written: u64,
	failing_rays: [16]u32,
	failing_count: u32,
}

ray_matching_hit :: proc(actual, expected: Ray_Hit) -> u32
{
	if actual.collider != expected.collider || actual.source != expected.source || math.is_nan(actual.distance) || math.is_inf(actual.distance) ||
		math.abs(actual.distance - expected.distance) > 1e-4 + 1e-5 * max(1, expected.distance)
	{
		return 0
	}
	length: f64 = f64(actual.normal.x) * f64(actual.normal.x) + f64(actual.normal.y) * f64(actual.normal.y) + f64(actual.normal.z) * f64(actual.normal.z)
	dot: f64 = f64(actual.normal.x) * f64(expected.normal.x) + f64(actual.normal.y) * f64(expected.normal.y) + f64(actual.normal.z) * f64(expected.normal.z)
	expected_length: f64 = f64(expected.normal.x) * f64(expected.normal.x) + f64(expected.normal.y) * f64(expected.normal.y) + f64(expected.normal.z) * f64(expected.normal.z)
	return u32(!math.is_nan(length) && !math.is_inf(length) && math.abs(length - 1) < 1e-4 && dot >= 0.9999984769132877 * math.sqrt(length * expected_length))
}

ray_validate_outputs :: proc(phase: ^Ray_Corpus_Phase, outputs: []Ray_Output, hits: []Ray_Hit) -> Ray_Validation
{
	validation: Ray_Validation
	if len(outputs) != len(phase.rays)
	{
		validation.errors = 1
		return validation
	}
	for output, index in outputs
	{
		range: Ray_Range = phase.expected[index]
		valid: u32 = u32(output.written == 1 && output.status == .Ok && u64(output.first) + u64(output.count) <= u64(len(hits)))
		validation.written += u64(output.written == 1)
		validation.hit_rays += u64(output.count != 0)
		if valid != 0 && (phase.phase == .Shadow || phase.phase == .Ambient)
		{
			valid = u32(output.count == u32(range.count != 0))
		}
		else if valid != 0 && phase.phase != .Collider_Hits && phase.phase != .Mesh_Hits
		{
			valid = u32(output.count == u32(range.count != 0))
			if valid != 0 && range.count != 0
			{
				valid = 0
				for candidate: u32 = 0; candidate < range.count; candidate += 1
				{
					valid |= ray_matching_hit(hits[output.first], phase.hits[range.first + candidate])
				}
			}
		}
		else if valid != 0
		{
			expected_count: u32
			for candidate: u32 = 0; candidate < range.count; candidate += 1
			{
				expected: Ray_Hit = phase.hits[range.first + candidate]
				unique: u32 = 1
				for prior: u32 = 0; prior < candidate; prior += 1
				{
					previous: Ray_Hit = phase.hits[range.first + prior]
					if previous.collider == expected.collider && (phase.phase == .Collider_Hits || previous.source == expected.source)
					{
						unique = 0
					}
				}
				expected_count += unique
			}
			valid = u32(output.count == expected_count)
			for event: u32 = 0; event < output.count && valid != 0; event += 1
			{
				matched: u32
				for candidate: u32 = 0; candidate < range.count; candidate += 1
				{
					matched |= ray_matching_hit(hits[output.first + event], phase.hits[range.first + candidate])
				}
				for prior: u32 = 0; prior < event; prior += 1
				{
					if hits[output.first + prior].collider == hits[output.first + event].collider &&
						(phase.phase == .Collider_Hits || hits[output.first + prior].source == hits[output.first + event].source)
					{
						matched = 0
					}
				}
				valid &= matched
			}
		}
		if valid == 0
		{
			validation.errors += 1
			if validation.failing_count < 16
			{
				validation.failing_rays[validation.failing_count] = u32(index)
				validation.failing_count += 1
			}
		}
	}
	return validation
}
