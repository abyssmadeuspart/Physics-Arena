package common

import "core:math"
import "core:mem"
import entasis "entasis:entasis"
import cooking "entasis:entasis_cooking"
import util "entasis:entasis_utilities"

World_State :: enum u8
{
	Empty,
	Ready,
}

Native_Shape :: struct
{
	handle: entasis.Shape_Handle,
	inertia: entasis.Body_Inertia,
	rotation: Quaternion,
	center: Vector3,
}

Runtime :: struct
{
	world: entasis.World,
	state: World_State,
	material: entasis.Default_Narrow_Policy,
	bodies: []entasis.Body_Handle,
	centers: []Vector3,
	body_count: u32,
	threads: u32,
}

shape_rotation :: proc(geometry: Geometry) -> Quaternion
{
	if geometry.shape == .Capsule
	{
		switch geometry.axis
		{
		case .X:
			return {z = -0.7071067811865475, w = 0.7071067811865475}
		case .Z:
			return {x = 0.7071067811865475, w = 0.7071067811865475}
		case .Y:
		}
	}
	return {w = 1}
}

shape_volume :: proc(geometry: Geometry) -> f32
{
	switch geometry.shape
	{
	case .Box:
		return 8 * geometry.half_extents.x * geometry.half_extents.y * geometry.half_extents.z
	case .Convex_Hull:
		return (47.0 / 6.0) * geometry.half_extents.x * geometry.half_extents.y * geometry.half_extents.z
	case .Sphere:
		return (4.0 / 3.0) * math.PI * geometry.radius * geometry.radius * geometry.radius
	case .Capsule:
		return math.PI * geometry.radius * geometry.radius * (2 * geometry.half_segment + (4.0 / 3.0) * geometry.radius)
	}
	unreachable()
}

native_shape :: proc(runtime: ^Runtime, geometry: Geometry, hull_points: []Vector3, density: f32) -> (Native_Shape, Status)
{
	shape: Native_Shape = {rotation = shape_rotation(geometry)}
	status: entasis.Status
	switch geometry.shape
	{
	case .Box:
		shape.handle, status = entasis.shape_add(&runtime.world,
			entasis.box_half_extents(geometry.half_extents.x, geometry.half_extents.y, geometry.half_extents.z))
	case .Sphere:
		shape.handle, status = entasis.shape_add(&runtime.world, entasis.sphere(geometry.radius))
	case .Capsule:
		shape.handle, status = entasis.shape_add(&runtime.world, entasis.capsule_half_length(geometry.radius, geometry.half_segment))
	case .Convex_Hull:
		cooker: cooking.Cooking_Context
		if cooking.cooking_context_init(&cooker) != .Ok
		{
			return {}, .Physics_Failed
		}
		defer cooking.cooking_context_destroy(&cooker)
		points: [24]Vector3
		for point, index in hull_points
		{
			points[index] = {point.x * geometry.half_extents.x, point.y * geometry.half_extents.y, point.z * geometry.half_extents.z}
		}
		cooked: cooking.Cooked_Hull
		cooked, status = cooking.cook_hull(&cooker, points[:])
		if status != .Ok
		{
			return {}, .Physics_Failed
		}
		defer cooking.cooked_hull_destroy(&cooked)
		shape.center = cooked.center
		shape.handle, status = cooking.cooked_hull_import(&runtime.world, &cooked)
	}
	if status != .Ok
	{
		return {}, .Physics_Failed
	}
	if density > 0
	{
		shape.inertia, status = entasis.shape_registered_inertia(&runtime.world, shape.handle, density * shape_volume(geometry))
		if status != .Ok
		{
			return {}, .Physics_Failed
		}
	}
	return shape, .Ok
}

benchmark_world_description :: proc(execution: ^Execution, threads: u32) -> entasis.World_Description
{
	description: entasis.World_Description = entasis.world_description_default()
	description.gravity = execution.gravity
	description.damping = {}
	description.capacity.bodies = i32(max(execution.dynamic_count + execution.kinematic_count, 1))
	description.capacity.statics = i32(execution.static_count)
	description.threading.worker_count = i32(threads)
	description.threading.worker_pool_block_size = 65536
	if execution.family == .Ray_Tracing
	{
		description.capacity.shapes_per_type = i32(max((execution.ray_tracing.primitive_count + 3) / 4, execution.ray_tracing.mesh_count))
		return description
	}
	if execution.family == .Query
	{
		description.capacity.shapes_per_type = 4
		return description
	}
	// native maintenance requires more than two total broad-phase leaf slots
	description.capacity.bodies = i32(max(execution.dynamic_count, 2))
	pairs: i32
	switch execution.family
	{
	case .Container:
		pairs = i32(max(u64(1), (u64(execution.dynamic_count) * 96 * 1024 + 9999) / 10000))
		// each support is registered separately, unlike the reference's shape cache
		description.capacity.shapes_per_type = i32(max(execution.static_count + u32(execution.geometry.shape == .Box), 1))
	case .Contact_Islands, .Pyramid, .Pyramid_Wall:
		pairs = i32(execution.dynamic_count * 12)
		description.capacity.shapes_per_type = 2
	case .Query, .Ray_Tracing:
		unreachable()
	}
	description.capacity.constraints = pairs
	description.capacity.pairs = pairs
	description.capacity.broad_phase_candidates = pairs
	description.capacity.inactive_body_sets = 1
	description.capacity.inactive_pairs = 1
	if execution.sleep_mode == 1
	{
		description.capacity.inactive_body_sets = i32(execution.dynamic_count + 1)
		description.capacity.inactive_pairs = pairs
	}
	description.capacity.initial_constraints_per_type_batch = 64
	description.capacity.minimum_constraints_per_body = 8
	description.capacity.pending_pairs_per_worker = 4096
	description.capacity.collision_child_pairs = i32(threads)
	description.solve.velocity_iterations = i32(execution.solver_values[.Velocity_Iterations])
	description.solve.substeps = i32(execution.solver_values[.Substeps])
	description.solve.fallback_batch_threshold = 64
	return description
}

runtime_init :: proc(runtime: ^Runtime, execution: ^Execution, threads: u32, pool: ^entasis.Buffer_Pool) -> Status
{
	allocation: mem.Allocator_Error
	runtime.bodies, allocation = make([]entasis.Body_Handle, int(execution.dynamic_count))
	if allocation != nil
	{
		return .Capacity
	}
	runtime.centers, allocation = make([]Vector3, int(execution.dynamic_count))
	if allocation != nil
	{
		return .Capacity
	}
	runtime.material = entasis.default_narrow_policy()
	runtime.material.material.friction_coefficient = execution.friction
	description: entasis.World_Description = benchmark_world_description(execution, threads)
	description.narrow_callbacks = entasis.narrow_policy_default(&runtime.material)
	native_status: entasis.Status
	if execution.family == .Query
	{
		native_status = entasis.world_init(&runtime.world, description)
	}
	else
	{
		native_status = entasis.world_init_with_pool(&runtime.world, description, pool)
	}
	if native_status != .Ok
	{
		return .Physics_Failed
	}
	runtime.state = .Ready
	runtime.threads = threads
	if execution.restitution > 0
	{
		restitution: entasis.Restitution_Configuration = entasis.restitution_configuration_default()
		restitution.fallback.coefficient = execution.restitution
		restitution.pair_capacity = description.capacity.pairs
		if entasis.world_enable_restitution(&runtime.world, restitution) != .Ok
		{
			return .Physics_Failed
		}
	}
	return .Ok
}

runtime_destroy :: proc(runtime: ^Runtime) -> Status
{
	if runtime.state == .Ready && entasis.world_destroy(&runtime.world) != .Ok
	{
		return .Physics_Failed
	}
	delete(runtime.bodies)
	delete(runtime.centers)
	runtime^ = {}
	return .Ok
}

add_dynamic :: proc(runtime: ^Runtime, shape: Native_Shape, position: Vector3, sleep_mode: u8, ccd_mode: u8) -> Status
{
	activity: entasis.Activity_Description = entasis.body_activity_default()
	if sleep_mode == 0
	{
		activity.sleep_threshold = -1
	}
	center: Vector3 = util.quaternion_transform(shape.center, shape.rotation)
	description: entasis.Body_Description = entasis.body_dynamic(shape.handle, shape.inertia,
		entasis.pose({position.x + center.x, position.y + center.y, position.z + center.z}, shape.rotation), {}, activity)
	description.collidable.continuity = entasis.ccd_discrete()
	if ccd_mode == 1
	{
		description.collidable.continuity = entasis.ccd_continuous()
	}
	status: entasis.Status
	runtime.bodies[runtime.body_count], status = entasis.body_add(&runtime.world, description)
	if status != .Ok
	{
		return .Physics_Failed
	}
	runtime.centers[runtime.body_count] = shape.center
	runtime.body_count += 1
	return .Ok
}

add_static :: proc(runtime: ^Runtime, shape: Native_Shape, position: Vector3) -> Status
{
	center: Vector3 = util.quaternion_transform(shape.center, shape.rotation)
	handle: entasis.Static_Handle
	status: entasis.Status
	handle, status = entasis.static_add(&runtime.world, entasis.static_body(shape.handle,
		entasis.pose({position.x + center.x, position.y + center.y, position.z + center.z}, shape.rotation)), .None)
	if status != .Ok
	{
		return .Physics_Failed
	}
	return .Ok
}

body_pose :: proc(runtime: ^Runtime, index: u32) -> (entasis.Rigid_Pose, Status)
{
	body: entasis.Body_State
	status: entasis.Status
	body, status = entasis.body_get(&runtime.world, runtime.bodies[index])
	if status != .Ok
	{
		return {}, .Physics_Failed
	}
	center: Vector3 = util.quaternion_transform(runtime.centers[index], body.pose.orientation)
	pose: entasis.Rigid_Pose = body.pose
	pose.position.x -= center.x
	pose.position.y -= center.y
	pose.position.z -= center.z
	return pose, .Ok
}
