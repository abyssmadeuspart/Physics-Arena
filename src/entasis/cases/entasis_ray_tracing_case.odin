package cases

import "base:runtime"
import "core:mem"
import "core:time"
import "polygon:common"
import entasis "entasis:entasis"
import physics "entasis:entasis_physics"
import cooking "entasis:entasis_cooking"
import util "entasis:entasis_utilities"

Ray_Lane :: struct
{
	query_context: entasis.Query_Context,
	native_hits: []entasis.Ray_Hit,
	native_storage: []entasis.Ray_Hit,
	status: common.Status,
}
Ray_Runtime :: struct
{
	world: entasis.World,
	state: common.World_State,
	scene: ^common.Ray_Scene,
	dispatcher: ^util.Thread_Dispatcher_Boundary,
	lanes: []Ray_Lane,
	body_ids, static_ids: []u32,
	body_handles: []entasis.Body_Handle,
	centers: []common.Vector3,
	triangle_map: []u32,
	phase: ^common.Ray_Corpus_Phase,
	api: common.Ray_Api,
	outputs: []common.Ray_Output,
	hits: []common.Ray_Hit,
	queries: []entasis.Query,
	batch_results: []entasis.Query_Result,
	output_storage: []common.Ray_Output,
	hit_storage: []common.Ray_Hit,
	query_storage: []entasis.Query,
	batch_storage: []entasis.Query_Result,
	input: common.Ray_Corpus_Phase,
	buffer_bytes: u64,
	setup_ms: f64,
}
Ray_Filter :: struct
{
	owner: ^Ray_Runtime,
	mask: u32,
}

ray_native_shape :: proc(owner: ^Ray_Runtime, collider: ^common.Ray_Collider, cooker: ^cooking.Cooking_Context) -> (entasis.Shape_Handle, common.Vector3, common.Status)
{
	shape: entasis.Shape_Handle
	center: common.Vector3
	status: entasis.Status
	switch collider.shape
	{
	case .Box:
		shape, status = entasis.shape_add(&owner.world, entasis.box_half_extents(collider.size.x, collider.size.y, collider.size.z))
	case .Sphere:
		shape, status = entasis.shape_add(&owner.world, entasis.sphere(collider.size.x))
	case .Capsule:
		shape, status = entasis.shape_add(&owner.world, entasis.capsule_half_length(collider.size.x, collider.size.y))
	case .Hull:
		if collider.hull >= u32(len(owner.scene.hulls))
		{
			return {}, {}, .Invalid
		}
		hull: common.Ray_Hull = owner.scene.hulls[collider.hull]
		points: [64]common.Vector3
		if hull.vertex_count > 64 || u64(hull.first_vertex) + u64(hull.vertex_count) > u64(len(owner.scene.vertices))
		{
			return {}, {}, .Invalid
		}
		for index: u32 = 0; index < hull.vertex_count; index += 1
		{
			p: common.Vector3 = owner.scene.vertices[hull.first_vertex + index]
			points[index] = {p.x * collider.size.x, p.y * collider.size.y, p.z * collider.size.z}
		}
		cooked: cooking.Cooked_Hull
		cooked, status = cooking.cook_hull(cooker, points[:hull.vertex_count])
		if status != .Ok
		{
			return {}, {}, .Physics_Failed
		}
		defer cooking.cooked_hull_destroy(&cooked)
		center = cooked.center
		shape, status = cooking.cooked_hull_import(&owner.world, &cooked)
	case .Mesh:
		if collider.triangle_count > 1024 || u64(collider.first_triangle) + u64(collider.triangle_count) > u64(len(owner.scene.triangles))
		{
			return {}, {}, .Invalid
		}
		triangles: [1024]entasis.Triangle
		for index: u32 = 0; index < collider.triangle_count; index += 1
		{
			t: common.Ray_Triangle = owner.scene.triangles[collider.first_triangle + index]
			triangles[index] = {a = t.a, b = t.c, c = t.b}
		}
		cooked: cooking.Cooked_Mesh
		cooked, status = cooking.cook_mesh(cooker, triangles[:collider.triangle_count])
		if status != .Ok
		{
			return {}, {}, .Physics_Failed
		}
		defer cooking.cooked_mesh_destroy(&cooked)
		for index: u32 = 0; index < collider.triangle_count; index += 1
		{
			if cooked.mesh.triangles.memory[index] != triangles[index]
			{
				return {}, {}, .Physics_Failed
			}
			owner.triangle_map[collider.first_triangle + index] = index
		}
		shape, status = cooking.cooked_mesh_import(&owner.world, &cooked)
	}
	if status != .Ok
	{
		return {}, {}, .Physics_Failed
	}
	return shape, center, .Ok
}

ray_native_pose :: proc(pose: common.Ray_Pose, center: common.Vector3) -> entasis.Rigid_Pose
{
	offset: common.Vector3 = util.quaternion_transform(center, pose.rotation)
	return entasis.pose({pose.position.x + offset.x, pose.position.y + offset.y, pose.position.z + offset.z}, pose.rotation)
}

ray_build :: proc(owner: ^Ray_Runtime, scene: ^common.Ray_Scene, execution: ^common.Execution, threads: u32) -> common.Status
{
	start: time.Tick = time.tick_now()
	owner.scene = scene
	allocation: mem.Allocator_Error
	owner.lanes, allocation = make([]Ray_Lane, int(threads))
	if allocation != nil
	{
		return .Capacity
	}
	owner.body_ids, allocation = make([]u32, len(scene.colliders))
	if allocation != nil
	{
		return .Capacity
	}
	owner.static_ids, allocation = make([]u32, len(scene.colliders))
	if allocation != nil
	{
		return .Capacity
	}
	owner.body_handles, allocation = make([]entasis.Body_Handle, len(scene.colliders))
	if allocation != nil
	{
		return .Capacity
	}
	owner.centers, allocation = make([]common.Vector3, len(scene.colliders))
	if allocation != nil
	{
		return .Capacity
	}
	owner.triangle_map, allocation = make([]u32, len(scene.triangles))
	if allocation != nil
	{
		return .Capacity
	}
	description: entasis.World_Description = common.benchmark_world_description(execution, threads)
	if entasis.world_init(&owner.world, description) != .Ok
	{
		return .Physics_Failed
	}
	owner.state = .Ready
	cooker: cooking.Cooking_Context
	if cooking.cooking_context_init(&cooker) != .Ok
	{
		return .Physics_Failed
	}
	defer cooking.cooking_context_destroy(&cooker)
	for &collider, index in scene.colliders
	{
		shape: entasis.Shape_Handle
		status: common.Status
		shape, owner.centers[index], status = ray_native_shape(owner, &collider, &cooker)
		if status != .Ok
		{
			return status
		}
		pose: entasis.Rigid_Pose = ray_native_pose(collider.pose, owner.centers[index])
		native_status: entasis.Status
		if collider.moving != 0
		{
			handle: entasis.Body_Handle
			handle, native_status = entasis.body_add(&owner.world, entasis.body_kinematic(shape, pose, {}, {sleep_threshold = -1}))
			if native_status != .Ok || handle.value < 0 || handle.value >= i32(len(owner.body_ids))
			{
				return .Physics_Failed
			}
			owner.body_ids[handle.value] = u32(index) + 1
			owner.body_handles[index] = handle
		}
		else
		{
			handle: entasis.Static_Handle
			handle, native_status = entasis.static_add(&owner.world, entasis.static_body(shape, pose), .None)
			if native_status != .Ok || handle.value < 0 || handle.value >= i32(len(owner.static_ids))
			{
				return .Physics_Failed
			}
			owner.static_ids[handle.value] = u32(index) + 1
		}
	}
	native_status: entasis.Status
	owner.dispatcher, native_status = entasis.world_borrow_dispatcher(&owner.world)
	if native_status != .Ok || (threads > 1 && (owner.dispatcher == nil || owner.dispatcher.worker_count != int(threads)))
	{
		return .Physics_Failed
	}
	for &lane in owner.lanes
	{
		if entasis.query_context_init(&lane.query_context, &owner.world) != .Ok
		{
			return .Physics_Failed
		}
	}
	owner.setup_ms = time.duration_milliseconds(time.tick_since(start))
	return .Ok
}

ray_reserve :: proc(owner: ^Ray_Runtime, capacity: common.Ray_Phase_Capacity) -> common.Status
{
	bytes: u64 = capacity.rays * u64(size_of(common.Ray_Input) + size_of(common.Ray_Range) + size_of(common.Ray_Output)) +
		(capacity.reference_hits + capacity.output_hits) * u64(size_of(common.Ray_Hit)) +
		capacity.batch_rays * u64(size_of(entasis.Query) + size_of(entasis.Query_Result)) +
		capacity.native_events * u64(len(owner.lanes)) * u64(size_of(entasis.Ray_Hit))
	if capacity.output_hits > u64(max(u32)) || bytes > common.RAY_ACTIVE_BYTE_LIMIT
	{
		return .Capacity
	}
	owner.buffer_bytes = bytes
	status: common.Status = common.ray_phase_reserve(&owner.input, capacity)
	if status != .Ok
	{
		return status
	}
	allocation: mem.Allocator_Error
	owner.output_storage, allocation = make([]common.Ray_Output, int(capacity.rays))
	if allocation != nil
	{
		return .Capacity
	}
	owner.hit_storage, allocation = make([]common.Ray_Hit, int(capacity.output_hits))
	if allocation != nil
	{
		return .Capacity
	}
	owner.query_storage, allocation = make([]entasis.Query, int(capacity.batch_rays))
	if allocation != nil
	{
		return .Capacity
	}
	owner.batch_storage, allocation = make([]entasis.Query_Result, int(capacity.batch_rays))
	if allocation != nil
	{
		return .Capacity
	}
	for &lane in owner.lanes
	{
		lane.native_storage, allocation = make([]entasis.Ray_Hit, int(capacity.native_events))
		if allocation != nil
		{
			return .Capacity
		}
	}
	return .Ok
}

ray_destroy :: proc(owner: ^Ray_Runtime)
{
	delete(owner.output_storage)
	delete(owner.hit_storage)
	delete(owner.query_storage)
	delete(owner.batch_storage)
	common.ray_phase_destroy(&owner.input)
	for &lane in owner.lanes
	{
		if lane.query_context != nil
		{
			entasis.query_context_destroy(&lane.query_context)
		}
		delete(lane.native_storage)
	}
	if owner.state == .Ready
	{
		entasis.world_destroy(&owner.world)
	}
	delete(owner.lanes)
	delete(owner.body_ids)
	delete(owner.static_ids)
	delete(owner.body_handles)
	delete(owner.centers)
	delete(owner.triangle_map)
	owner^ = {}
}

ray_publish_poses :: proc(owner: ^Ray_Runtime, view: int) -> (f64, common.Status)
{
	start: time.Tick = time.tick_now()
	for &collider, index in owner.scene.colliders
	{
		if collider.moving == 0
		{
			continue
		}
		pose: common.Ray_Pose = collider.pose
		if view >= 0
		{
			pose = common.ray_updated_pose(&collider, u32(view))
		}
		if entasis.body_set_pose(&owner.world, owner.body_handles[index], ray_native_pose(pose, owner.centers[index])) != .Ok
		{
			return 0, .Physics_Failed
		}
	}
	return time.duration_milliseconds(time.tick_since(start)), .Ok
}

ray_stable_id :: proc "contextless" (owner: ^Ray_Runtime, reference: entasis.Collidable_Reference) -> u32
{
	handle: i32 = physics.collidable_reference_raw_handle(reference)
	if handle < 0 || handle >= i32(len(owner.body_ids))
	{
		return 0
	}
	if physics.collidable_reference_mobility(reference) == .Static
	{
		return owner.static_ids[handle]
	}
	return owner.body_ids[handle]
}

ray_allow :: proc "contextless" (context_pointer: rawptr, reference: entasis.Collidable_Reference) -> bool
{
	filter: ^Ray_Filter = cast(^Ray_Filter)context_pointer
	id: u32 = ray_stable_id(filter.owner, reference)
	return id != 0 && (filter.owner.scene.colliders[id - 1].category & filter.mask) != 0
}

ray_map_hit :: proc(owner: ^Ray_Runtime, input: ^common.Ray_Input, native: entasis.Ray_Hit) -> (common.Ray_Hit, common.Status)
{
	id: u32 = ray_stable_id(owner, native.collidable)
	if id == 0
	{
		return {}, .Physics_Failed
	}
	source: u32 = max(u32)
	collider: ^common.Ray_Collider = &owner.scene.colliders[id - 1]
	if collider.shape == .Mesh
	{
		if native.child_index < 0 || u32(native.child_index) >= collider.triangle_count
		{
			return {}, .Physics_Failed
		}
		source = owner.triangle_map[collider.first_triangle + u32(native.child_index)]
	}
	return {distance = f64(native.t) * f64(input.length), normal = native.normal, collider = id, source = source}, .Ok
}

ray_prepare_phase :: proc(owner: ^Ray_Runtime, phase: ^common.Ray_Corpus_Phase, api: common.Ray_Api) -> common.Status
{
	capacity: u64
	for expected in phase.expected
	{
		capacity += u64(expected.count) + 4 if phase.phase == .Collider_Hits || phase.phase == .Mesh_Hits else 1
	}
	if capacity > u64(len(owner.hit_storage)) || len(phase.rays) > len(owner.output_storage)
	{
		return .Capacity
	}
	if api == .Native_Batch
	{
		if phase.phase != .Primary && phase.phase != .Shuffled && phase.phase != .Reflection && phase.phase != .Updated
		{
			return .Invalid
		}
		if len(phase.rays) > len(owner.query_storage) || len(phase.rays) > len(owner.batch_storage)
		{
			return .Capacity
		}
	}
	for lane in owner.lanes
	{
		if u64(phase.native_event_capacity) > u64(len(lane.native_storage))
		{
			return .Capacity
		}
	}
	owner.phase = phase
	owner.api = api
	owner.outputs = owner.output_storage[:len(phase.rays)]
	owner.hits = owner.hit_storage[:capacity]
	owner.queries = owner.query_storage[:len(phase.rays)] if api == .Native_Batch else nil
	owner.batch_results = owner.batch_storage[:len(phase.rays)] if api == .Native_Batch else nil
	offset: u32
	for expected, index in phase.expected
	{
		owner.outputs[index] = {first = offset}
		offset += expected.count + 4 if phase.phase == .Collider_Hits || phase.phase == .Mesh_Hits else 1
	}
	for &lane in owner.lanes
	{
		lane.native_hits = lane.native_storage[:phase.native_event_capacity]
	}
	if api == .Native_Batch
	{
		for input, index in phase.rays
		{
			owner.queries[index] = entasis.query_ray_closest({origin = input.origin, direction = input.translation, maximum_t = 1})
		}
	}
	return .Ok
}

ray_query_one :: proc(owner: ^Ray_Runtime, lane: ^Ray_Lane, index: u32) -> common.Status
{
	input: ^common.Ray_Input = &owner.phase.rays[index]
	output: ^common.Ray_Output = &owner.outputs[index]
	ray: entasis.Ray = {origin = input.origin, direction = input.translation, maximum_t = 1}
	filter_context: Ray_Filter = {owner = owner, mask = input.mask}
	filter: entasis.Query_Filter
	if input.mask != 15
	{
		filter = {allow = ray_allow, user_context = &filter_context}
	}
	native_status: entasis.Status
	if owner.phase.phase == .Shadow || owner.phase.phase == .Ambient
	{
		presence: entasis.Query_Hit_State
		presence, native_status = entasis.ray_cast_any_with_context(&lane.query_context, ray, filter)
		output.count = u32(presence == .Hit)
	}
	else if owner.phase.phase == .Collider_Hits || owner.phase.phase == .Mesh_Hits
	{
		count: int
		count, native_status = entasis.ray_cast_all_with_context(&lane.query_context, ray, lane.native_hits, filter)
		if native_status != .Ok
		{
			return .Capacity if native_status == .Capacity_Missing else .Physics_Failed
		}
		capacity: u32 = owner.phase.expected[index].count + 4
		for native in lane.native_hits[:count]
		{
			hit: common.Ray_Hit
			status: common.Status
			hit, status = ray_map_hit(owner, input, native)
			if status != .Ok
			{
				return status
			}
			slot: u32 = output.count
			if owner.phase.phase == .Collider_Hits
			{
				for candidate: u32 = 0; candidate < output.count; candidate += 1
				{
					if owner.hits[output.first + candidate].collider == hit.collider
					{
						slot = candidate
						break
					}
				}
			}
			if slot >= capacity
			{
				return .Capacity
			}
			if slot == output.count
			{
				owner.hits[output.first + slot] = hit
				output.count += 1
			}
			else if hit.distance < owner.hits[output.first + slot].distance
			{
				owner.hits[output.first + slot] = hit
			}
		}
	}
	else
	{
		native: entasis.Ray_Hit
		native, native_status = entasis.ray_cast_closest_with_context(&lane.query_context, ray, filter)
		if native_status == .Ok
		{
			status: common.Status
			owner.hits[output.first], status = ray_map_hit(owner, input, native)
			if status != .Ok
			{
				return status
			}
			output.count = 1
		}
	}
	if native_status != .Ok && native_status != .Not_Found
	{
		return .Physics_Failed
	}
	output.written = 1
	return .Ok
}

ray_lane :: proc(owner: ^Ray_Runtime, lane_index: u32) -> common.Status
{
	lane: ^Ray_Lane = &owner.lanes[lane_index]
	range: Query_Range = query_range(u32(len(owner.phase.rays)), lane_index, u32(len(owner.lanes)))
	if owner.api == .Native_Batch
	{
		status: entasis.Status = entasis.query_batch(&owner.world, owner.queries[range.begin:range.end], owner.batch_results[range.begin:range.end])
		if status != .Ok && status != .Not_Found
		{
			return .Physics_Failed
		}
		for index: u32 = range.begin; index < range.end; index += 1
		{
			result: entasis.Query_Result = owner.batch_results[index]
			output: ^common.Ray_Output = &owner.outputs[index]
			if result.status != .Ok && result.status != .Not_Found
			{
				return .Physics_Failed
			}
			output.count = u32(result.hit)
			if result.hit
			{
				mapped: common.Status
				owner.hits[output.first], mapped = ray_map_hit(owner, &owner.phase.rays[index], result.ray_hit)
				if mapped != .Ok
				{
					return mapped
				}
			}
			output.written = 1
		}
		return .Ok
	}
	for index: u32 = range.begin; index < range.end; index += 1
	{
		status: common.Status = ray_query_one(owner, lane, index)
		if status != .Ok
		{
			owner.outputs[index].status = .Failed
			return status
		}
	}
	return .Ok
}

ray_worker :: proc "contextless" (worker_index: int, dispatcher: ^util.Thread_Dispatcher_Boundary)
{
	context = runtime.default_context()
	owner: ^Ray_Runtime = cast(^Ray_Runtime)dispatcher.unmanaged_context
	owner.lanes[worker_index].status = ray_lane(owner, u32(worker_index))
}

ray_reset_outputs :: proc(owner: ^Ray_Runtime)
{
	for &output in owner.outputs
	{
		output.count = 0
		output.written = 0
		output.status = .Ok
	}
	for &lane in owner.lanes
	{
		lane.status = .Ok
	}
}

ray_execute :: proc(owner: ^Ray_Runtime) -> (f64, common.Status)
{
	start: time.Tick = time.tick_now()
	if len(owner.lanes) == 1
	{
		owner.lanes[0].status = ray_lane(owner, 0)
	}
	else if owner.dispatcher.dispatch(owner.dispatcher, ray_worker, len(owner.lanes), owner) != .Ok
	{
		return 0, .Physics_Failed
	}
	elapsed: f64 = time.duration_milliseconds(time.tick_since(start))
	for lane in owner.lanes
	{
		if lane.status != .Ok
		{
			return elapsed, lane.status
		}
	}
	return elapsed, .Ok
}
