package common

import "core:encoding/endian"
import "core:encoding/hex"
import "core:math"
import "core:strings"
import entasis "entasis:entasis"

Status :: enum u8
{
	Ok,
	Invalid,
	Capacity,
	Physics_Failed,
	Io_Failed,
	Cancelled,
}

Family :: enum u16
{
	Container = 1,
	Contact_Islands = 2,
	Query = 3,
	Pyramid = 5,
	Pyramid_Wall = 6,
	Ray_Tracing = 7,
}

Preset :: enum u8
{
	Authored,
	Sphere,
	Capsule,
	Convex_Hull,
}

Shape :: enum u8
{
	Box = 1,
	Sphere,
	Capsule,
	Convex_Hull,
}

Axis :: enum u8
{
	Y,
	X,
	Z,
}

Vector3 :: entasis.Vector3
Quaternion :: entasis.Quaternion

Geometry :: struct
{
	shape: Shape,
	half_extents: Vector3,
	radius: f32,
	half_segment: f32,
	axis: Axis,
}

Box :: struct
{
	center: Vector3,
	half_extents: Vector3,
}

Container :: struct
{
	grid: [3]u32,
	half_extents: Vector3,
	spacing: Vector3,
	initial_y: f32,
	density: f32,
	static_boxes: [16]Box,
	static_count: u16,
}

Contact_Islands :: struct
{
	island_grid: [2]u32,
	island_spacing: [2]f32,
	body_grid: [3]u32,
	half_extents: Vector3,
	body_spacing: Vector3,
	initial_y: f32,
	floor_half_extents: Vector3,
	density: f32,
}

Spatial_Query :: struct
{
	grid: [3]u32,
	half_extents: Vector3,
	spacing: Vector3,
	base_center: Vector3,
	ray_count: u32,
	sphere_cast_count: u32,
	overlap_count: u32,
	distance: f32,
	sphere_cast_radius: f32,
	overlap_half_extents: Vector3,
	miss_offset: f32,
	debug_samples: u32,
}

Pyramid :: struct
{
	rows: u32,
	half_extents: Vector3,
	spacing: Vector3,
	base_center: Vector3,
	floor_half_extents: Vector3,
	density: f32,
	projectile_count: u32,
	projectile_radius: f32,
	projectile_density: f32,
	projectile_center: Vector3,
	projectile_spacing: Vector3,
	projectile_velocity: Vector3,
	launch_after: u32,
}

Pyramid_Wall :: struct
{
	rows: u32,
	half_extent: f32,
	density: f32,
	floor_half_extents: Vector3,
}

Ray_Tracing :: struct
{
	recipe_revision: u32,
	width: u32,
	height: u32,
	view_count: u32,
	primitive_count: u32,
	mesh_count: u32,
	triangles_per_mesh: u32,
	moving_count: u32,
	seed_low: u32,
	seed_high: u32,
}

Solver_Field :: enum u32
{
	Velocity_Iterations,
	Position_Iterations,
	Projection_Iterations,
	Solver_Iterations,
	Substeps,
	Collision_Steps,
}

Replay_Camera :: struct
{
	direction: Vector3,
	up: Vector3,
	minimum: Vector3,
	maximum: Vector3,
	eye: Vector3,
	target: Vector3,
	eye_offset: Vector3,
	target_offset: Vector3,
	vertical_fov_degrees: f32,
	viewport_fill: f32,
	near_plane: f32,
	far_plane: f32,
	stable_slot: u32,
	mode: u32,
}

Execution :: struct
{
	family: Family,
	preset: Preset,
	geometry: Geometry,
	hull_points: [24]Vector3,
	case_id: string,
	semantic: string,
	revision: u32,
	dynamic_count: u32,
	kinematic_count: u32,
	static_count: u32,
	body_count: u32,
	shape_count: u32,
	instance_count: u32,
	triangle_count: u32,
	query_count: u32,
	constraint_count: u32,
	timestep_present: u8,
	timestep_hz: u32,
	warmup_count: u32,
	measured_count: u32,
	debug_count: u32,
	gravity: Vector3,
	sleep_mode: u8,
	ccd_mode: u8,
	friction: f32,
	restitution: f32,
	replay_camera: Replay_Camera,
	solver_fields: u32,
	solver_values: [Solver_Field]u32,
	container: Container,
	islands: Contact_Islands,
	query: Spatial_Query,
	pyramid: Pyramid,
	pyramid_wall: Pyramid_Wall,
	ray_tracing: Ray_Tracing,
}

Reader :: struct
{
	bytes: []u8,
	offset: int,
	status: Status,
}

read_u8 :: proc(reader: ^Reader) -> u8
{
	if reader.offset == len(reader.bytes)
	{
		reader.status = .Invalid
		return 0
	}
	value: u8 = reader.bytes[reader.offset]
	reader.offset += 1
	return value
}

read_u16 :: proc(reader: ^Reader) -> u16
{
	value: u16
	ok: bool
	value, ok = endian.get_u16(reader.bytes[reader.offset:], .Little)
	if !ok
	{
		reader.status = .Invalid
		return 0
	}
	reader.offset += 2
	return value
}

read_u32 :: proc(reader: ^Reader) -> u32
{
	value: u32
	ok: bool
	value, ok = endian.get_u32(reader.bytes[reader.offset:], .Little)
	if !ok
	{
		reader.status = .Invalid
		return 0
	}
	reader.offset += 4
	return value
}

read_f32 :: proc(reader: ^Reader) -> f32
{
	bits: u32 = read_u32(reader)
	value: f32 = transmute(f32)bits
	if math.is_nan(value) || math.is_inf(value)
	{
		reader.status = .Invalid
	}
	return value
}

read_vector :: proc(reader: ^Reader) -> Vector3
{
	return {read_f32(reader), read_f32(reader), read_f32(reader)}
}

read_text :: proc(reader: ^Reader) -> string
{
	count: int = int(read_u16(reader))
	if count == 0 || count >= 64 || count > len(reader.bytes) - reader.offset
	{
		reader.status = .Invalid
		return ""
	}
	value: string = string(reader.bytes[reader.offset:reader.offset + count])
	reader.offset += count
	for character in transmute([]u8)value
	{
		if !(character >= 'a' && character <= 'z') && !(character >= '0' && character <= '9') && character != '_'
		{
			reader.status = .Invalid
		}
	}
	return value
}

read_geometry :: proc(reader: ^Reader) -> Geometry
{
	geometry: Geometry = {shape = Shape(read_u8(reader))}
	switch geometry.shape
	{
	case .Box, .Convex_Hull:
		geometry.half_extents = read_vector(reader)
	case .Sphere:
		geometry.radius = read_f32(reader)
	case .Capsule:
		geometry.radius = read_f32(reader)
		geometry.half_segment = read_f32(reader)
		geometry.axis = Axis(read_u8(reader))
	case:
		reader.status = .Invalid
	}
	return geometry
}

positive_vector :: proc(value: Vector3) -> Status
{
	if value.x <= 0 || value.y <= 0 || value.z <= 0
	{
		return .Invalid
	}
	return .Ok
}

grid_count :: proc(grid: [3]u32) -> u64
{
	if grid[0] == 0 || grid[1] == 0 || grid[2] == 0 || grid[0] > 16211 || grid[1] > 16211 || grid[2] > 16211
	{
		return 0
	}
	return u64(grid[0]) * u64(grid[1]) * u64(grid[2])
}

canonical_hull_point :: proc(index: u32) -> Vector3
{
	axis: u32 = index / 8
	signs: u32 = index % 8
	point: [3]f32
	for component: u32 = 0; component < 3; component += 1
	{
		point[component] = -1
		if (signs & (1 << component)) != 0
		{
			point[component] = 1
		}
		if component == axis
		{
			point[component] *= 0.5
		}
	}
	return {point[0], point[1], point[2]}
}

validate_execution :: proc(execution: ^Execution) -> Status
{
	e: ^Execution = execution
	if e.family == .Query || e.family == .Ray_Tracing
	{
		empty_solver_values: [Solver_Field]u32
		if e.solver_fields != 0 || e.solver_values != empty_solver_values
		{
			return .Invalid
		}
	}
	else if e.solver_fields != 17 || e.solver_values[.Velocity_Iterations] == 0 ||
		e.solver_values[.Velocity_Iterations] > 0x7fffffff || e.solver_values[.Substeps] == 0 ||
		e.solver_values[.Substeps] > 0x7fffffff || e.solver_values[.Position_Iterations] != 0 ||
		e.solver_values[.Projection_Iterations] != 0 || e.solver_values[.Solver_Iterations] != 0 ||
		e.solver_values[.Collision_Steps] != 0
	{
		return .Invalid
	}
	if e.family == .Ray_Tracing
	{
		r: ^Ray_Tracing = &e.ray_tracing
		if e.case_id != "ray_tracing_heavy" || e.semantic != "ray_tracing" || e.revision != 1 || e.preset != .Authored ||
			r.recipe_revision != 1 || r.width == 0 || r.width > 1920 || r.height == 0 || r.height > 1080 ||
			r.view_count == 0 || r.view_count > 6 || r.primitive_count == 0 || r.primitive_count > 65536 ||
			r.mesh_count > 1024 || r.triangles_per_mesh == 0 || r.triangles_per_mesh > 1024 || r.moving_count > r.primitive_count ||
			e.dynamic_count != 0 || e.kinematic_count != r.moving_count || e.static_count != r.primitive_count + r.mesh_count - r.moving_count ||
			e.body_count != r.primitive_count + r.mesh_count || e.shape_count != e.body_count || e.triangle_count != r.mesh_count * r.triangles_per_mesh ||
			e.query_count != r.width * r.height || e.constraint_count != 0 || e.instance_count != 0 || e.debug_count != 0 ||
			e.timestep_present != 0 || e.timestep_hz != 0 || e.warmup_count > 1000000 || e.measured_count == 0 || e.measured_count > 1000000 ||
			e.sleep_mode != 0 || e.ccd_mode != 0 || e.gravity != (Vector3{}) || e.friction != 0 || e.restitution != 0
		{
			return .Invalid
		}
		return .Ok
	}
	dynamic_capacity: u32 = 16210
	if e.family == .Pyramid_Wall
	{
		dynamic_capacity = 16290
	}
	if e.revision == 0 || e.measured_count == 0 || e.measured_count > 1000000 || e.warmup_count > 1000000 ||
		e.body_count == 0 || e.body_count > dynamic_capacity + 1 || u64(e.dynamic_count) + u64(e.kinematic_count) + u64(e.static_count) != u64(e.body_count) ||
		e.shape_count != e.body_count || e.instance_count != e.body_count || e.kinematic_count != 0 ||
		e.constraint_count != 0 || e.triangle_count != 0 || e.dynamic_count > dynamic_capacity || e.debug_count > 768 ||
		e.timestep_present > 1 || (e.timestep_present == 0) != (e.timestep_hz == 0) ||
		e.sleep_mode > 1 || e.ccd_mode > 1 || e.friction < 0 ||
		!(e.restitution >= 0 && e.restitution <= 1) || u8(e.preset) > 3
	{
		return .Invalid
	}
	base_id: string
	base_semantic: string
	half: Vector3
	switch e.family
	{
	case .Container:
		base_id = "box_container_pile_10k"
		base_semantic = "open_container_falling_pile"
		c: ^Container = &e.container
		half = c.half_extents
		if grid_count(c.grid) != u64(e.dynamic_count) || c.static_count == 0 || u32(c.static_count) != e.static_count ||
			positive_vector(c.half_extents) != .Ok || positive_vector(c.spacing) != .Ok || c.density <= 0 ||
			e.query_count != 0 || e.timestep_hz == 0 || e.debug_count != 0
		{
			return .Invalid
		}
		for index in 0..<int(c.static_count)
		{
			if positive_vector(c.static_boxes[index].half_extents) != .Ok
			{
				return .Invalid
			}
		}
	case .Contact_Islands:
		base_id = "box_contact_islands_10k"
		base_semantic = base_id
		i: ^Contact_Islands = &e.islands
		half = i.half_extents
		island_count: u64 = u64(i.island_grid[0]) * u64(i.island_grid[1])
		bodies_per_island: u64 = grid_count(i.body_grid)
		if island_count == 0 || island_count != u64(e.static_count) || bodies_per_island == 0 ||
			island_count * bodies_per_island != u64(e.dynamic_count) ||
			i.island_spacing[0] <= 0 || i.island_spacing[1] <= 0 ||
			positive_vector(i.half_extents) != .Ok || positive_vector(i.body_spacing) != .Ok ||
			positive_vector(i.floor_half_extents) != .Ok || i.density <= 0 ||
			e.query_count != 0 || e.debug_count != 0 || e.timestep_hz == 0
		{
			return .Invalid
		}
	case .Query:
		base_id = "spatial_query_trace"
		base_semantic = base_id
		q: ^Spatial_Query = &e.query
		half = q.half_extents
		if grid_count(q.grid) != u64(e.static_count) || e.dynamic_count != 0 || e.sleep_mode != 0 || e.ccd_mode != 0 || e.restitution != 0 ||
			u64(q.ray_count) + u64(q.sphere_cast_count) + u64(q.overlap_count) != u64(e.query_count) ||
			q.ray_count == 0 || q.sphere_cast_count == 0 || q.overlap_count == 0 || e.query_count > 100000 ||
			positive_vector(q.half_extents) != .Ok || positive_vector(q.spacing) != .Ok ||
			positive_vector(q.overlap_half_extents) != .Ok || q.distance <= 0 || q.sphere_cast_radius <= 0 || q.miss_offset <= 0 ||
			q.debug_samples > 256 || q.debug_samples > q.ray_count ||
			q.debug_samples > q.sphere_cast_count || q.debug_samples > q.overlap_count ||
			e.debug_count != 3 * q.debug_samples || e.timestep_present != 0
		{
			return .Invalid
		}
	case .Pyramid:
		base_id = "large_pyramid_16206"
		base_semantic = "large_pyramid"
		p: ^Pyramid = &e.pyramid
		half = p.half_extents
		if e.preset != .Authored || p.rows == 0 || p.rows > 36 || p.projectile_count == 0 ||
			u64(p.rows) * u64(p.rows + 1) * u64(2 * p.rows + 1) / 6 + u64(p.projectile_count) != u64(e.dynamic_count) ||
			e.static_count != 1 || positive_vector(p.half_extents) != .Ok || positive_vector(p.spacing) != .Ok ||
			positive_vector(p.floor_half_extents) != .Ok || p.density <= 0 || p.projectile_density <= 0 || p.projectile_radius <= 0 ||
			p.launch_after == 0 || e.query_count != 0 || e.debug_count != 0 || e.timestep_hz == 0
		{
			return .Invalid
		}
	case .Pyramid_Wall:
		base_id = "pyramid_wall_4095"
		base_semantic = "pyramid_wall"
		w: ^Pyramid_Wall = &e.pyramid_wall
		half = {w.half_extent, w.half_extent, w.half_extent}
		count: u64 = u64(w.rows) * (u64(w.rows) + 1) / 2
		if e.preset != .Authored || w.rows == 0 || w.rows > 180 || count > 16290 || w.half_extent <= 0 || w.density <= 0 ||
			positive_vector(w.floor_half_extents) != .Ok || u64(e.dynamic_count) != count || e.static_count != 1 ||
			e.query_count != 0 || e.debug_count != 0 || e.timestep_hz == 0
		{
			return .Invalid
		}
	case .Ray_Tracing:
		unreachable()
	case:
		return .Invalid
	}
	suffixes: [4]string = {"", "_sphere", "_capsule", "_convex_hull"}
	suffix: string = suffixes[u8(e.preset)]
	if !strings.has_prefix(e.case_id, base_id) || e.case_id[len(base_id):] != suffix ||
		!strings.has_prefix(e.semantic, base_semantic) || e.semantic[len(base_semantic):] != suffix ||
		u8(e.geometry.shape) != u8(e.preset) + 1
	{
		return .Invalid
	}
	switch e.geometry.shape
	{
	case .Box, .Convex_Hull:
		if e.geometry.half_extents != half || e.geometry.axis != .Y
		{
			return .Invalid
		}
	case .Sphere:
		if e.geometry.radius != min(half.x, half.y, half.z) || e.geometry.axis != .Y
		{
			return .Invalid
		}
	case .Capsule:
		axis: Axis = .Y
		longest: f32 = half.y
		transverse: f32 = min(half.x, half.z)
		if half.x > longest
		{
			axis = .X
			longest = half.x
			transverse = min(half.y, half.z)
		}
		if half.z > longest
		{
			axis = .Z
			longest = half.z
			transverse = min(half.x, half.y)
		}
		if e.geometry.radius != transverse * 0.5 || e.geometry.half_segment != longest - transverse * 0.5 || e.geometry.axis != axis
		{
			return .Invalid
		}
	}
	if e.geometry.shape == .Convex_Hull
	{
		for point, index in e.hull_points
		{
			if point != canonical_hull_point(u32(index))
			{
				return .Invalid
			}
		}
	}
	return .Ok
}

decode_execution :: proc(bytes: []u8) -> (Execution, Status)
{
	if len(bytes) < 10 || len(bytes) > 2048 || string(bytes[:4]) != "PACX"
	{
		return {}, .Invalid
	}
	r: Reader = {bytes = bytes, offset = 4}
	e: Execution = {family = Family(read_u16(&r))}
	if read_u32(&r) != u32(len(bytes)) ||
		(e.family != .Container && e.family != .Contact_Islands && e.family != .Query && e.family != .Pyramid &&
		 e.family != .Pyramid_Wall && e.family != .Ray_Tracing)
	{
		return {}, .Invalid
	}
	e.case_id = read_text(&r)
	e.semantic = read_text(&r)
	e.revision = read_u32(&r)
	counts: [9]^u32 = {&e.dynamic_count, &e.kinematic_count, &e.static_count, &e.body_count,
		&e.shape_count, &e.instance_count, &e.triangle_count, &e.query_count, &e.constraint_count}
	for field in counts
	{
		field^ = read_u32(&r)
	}
	e.timestep_present = read_u8(&r)
	e.timestep_hz = read_u32(&r)
	e.warmup_count = read_u32(&r)
	e.measured_count = read_u32(&r)
	e.debug_count = read_u32(&r)
	e.gravity = read_vector(&r)
	e.sleep_mode = read_u8(&r)
	e.ccd_mode = read_u8(&r)
	e.friction = read_f32(&r)
	e.restitution = read_f32(&r)
	e.solver_fields = read_u32(&r)
	for field in Solver_Field
	{
		e.solver_values[field] = read_u32(&r)
	}
	e.replay_camera.direction = read_vector(&r)
	e.replay_camera.up = read_vector(&r)
	e.replay_camera.minimum = read_vector(&r)
	e.replay_camera.maximum = read_vector(&r)
	e.replay_camera.eye = read_vector(&r)
	e.replay_camera.target = read_vector(&r)
	e.replay_camera.eye_offset = read_vector(&r)
	e.replay_camera.target_offset = read_vector(&r)
	e.replay_camera.vertical_fov_degrees = read_f32(&r)
	e.replay_camera.viewport_fill = read_f32(&r)
	e.replay_camera.near_plane = read_f32(&r)
	e.replay_camera.far_plane = read_f32(&r)
	e.replay_camera.stable_slot = read_u32(&r)
	e.replay_camera.mode = read_u32(&r)
	e.preset = Preset(read_u8(&r))
	if e.preset == .Convex_Hull
	{
		for index in 0..<len(e.hull_points)
		{
			e.hull_points[index] = read_vector(&r)
		}
	}
	if e.family != .Ray_Tracing
	{
		e.geometry = read_geometry(&r)
	}
	switch e.family
	{
	case .Container:
		c: ^Container = &e.container
		for index in 0..<3
		{
			c.grid[index] = read_u32(&r)
		}
		c.half_extents = read_vector(&r)
		c.spacing = read_vector(&r)
		c.initial_y = read_f32(&r)
		c.density = read_f32(&r)
		c.static_count = read_u16(&r)
		if int(c.static_count) > len(c.static_boxes)
		{
			return {}, .Invalid
		}
		for index in 0..<int(c.static_count)
		{
			c.static_boxes[index] = {read_vector(&r), read_vector(&r)}
		}
	case .Contact_Islands:
		i: ^Contact_Islands = &e.islands
		for index in 0..<2
		{
			i.island_grid[index] = read_u32(&r)
		}
		for index in 0..<2
		{
			i.island_spacing[index] = read_f32(&r)
		}
		for index in 0..<3
		{
			i.body_grid[index] = read_u32(&r)
		}
		i.half_extents = read_vector(&r)
		i.body_spacing = read_vector(&r)
		i.initial_y = read_f32(&r)
		i.floor_half_extents = read_vector(&r)
		i.density = read_f32(&r)
	case .Query:
		q: ^Spatial_Query = &e.query
		for index in 0..<3
		{
			q.grid[index] = read_u32(&r)
		}
		q.half_extents = read_vector(&r)
		q.spacing = read_vector(&r)
		q.base_center = read_vector(&r)
		q.ray_count = read_u32(&r)
		q.sphere_cast_count = read_u32(&r)
		q.overlap_count = read_u32(&r)
		q.distance = read_f32(&r)
		q.sphere_cast_radius = read_f32(&r)
		q.overlap_half_extents = read_vector(&r)
		q.miss_offset = read_f32(&r)
		q.debug_samples = read_u32(&r)
	case .Pyramid:
		p: ^Pyramid = &e.pyramid
		p.rows = read_u32(&r)
		p.half_extents = read_vector(&r)
		p.spacing = read_vector(&r)
		p.base_center = read_vector(&r)
		p.floor_half_extents = read_vector(&r)
		p.density = read_f32(&r)
		p.projectile_count = read_u32(&r)
		p.projectile_radius = read_f32(&r)
		p.projectile_density = read_f32(&r)
		p.projectile_center = read_vector(&r)
		p.projectile_spacing = read_vector(&r)
		p.projectile_velocity = read_vector(&r)
		p.launch_after = read_u32(&r)
	case .Pyramid_Wall:
		w: ^Pyramid_Wall = &e.pyramid_wall
		w.rows = read_u32(&r)
		w.half_extent = read_f32(&r)
		w.density = read_f32(&r)
		w.floor_half_extents = read_vector(&r)
	case .Ray_Tracing:
		t: ^Ray_Tracing = &e.ray_tracing
		fields: [10]^u32 = {&t.recipe_revision, &t.width, &t.height, &t.view_count, &t.primitive_count,
			&t.mesh_count, &t.triangles_per_mesh, &t.moving_count, &t.seed_low, &t.seed_high}
		for field in fields
		{
			field^ = read_u32(&r)
		}
	}
	if r.status != .Ok || r.offset != len(bytes) || validate_execution(&e) != .Ok
	{
		return {}, .Invalid
	}
	return e, .Ok
}

decode_execution_hex :: proc(text: string, storage: []u8) -> (Execution, Status)
{
	if len(text) == 0 || len(text) > 4096 || len(text) % 2 != 0 || len(storage) < len(text) / 2
	{
		return {}, .Invalid
	}
	for value in transmute([]u8)text
	{
		if !(value >= '0' && value <= '9') && !(value >= 'a' && value <= 'f')
		{
			return {}, .Invalid
		}
	}
	bytes: []u8
	ok: bool
	bytes, ok = hex.decode_into_buffer(transmute([]u8)text, storage)
	if !ok
	{
		return {}, .Invalid
	}
	return decode_execution(bytes)
}
