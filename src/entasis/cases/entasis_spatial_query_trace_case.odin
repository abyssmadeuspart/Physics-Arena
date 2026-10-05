package cases

import "polygon:common"
import "base:runtime"
import "core:fmt"
import "core:mem"
import "core:time"
import entasis "entasis:entasis"
import physics "entasis:entasis_physics"
import util "entasis:entasis_utilities"

Query_Kind :: enum u8
{
	Ray,
	Sphere_Cast,
	Overlap,
}

Query_Input :: struct
{
	kind: Query_Kind,
	origin: common.Vector3,
	direction: common.Vector3,
	expected_hit: u32,
}

Query_Range :: struct
{
	begin: u32,
	end: u32,
}

query_range :: proc(count, lane, lane_count: u32) -> Query_Range
{
	return {u32(u64(count) * u64(lane) / u64(lane_count)), u32(u64(count) * u64(lane + 1) / u64(lane_count))}
}

query_position :: proc(fixture: ^common.Spatial_Query, index: u32) -> common.Vector3
{
	x: u32 = index % fixture.grid[0]
	z: u32 = (index / fixture.grid[0]) % fixture.grid[2]
	y: u32 = index / (fixture.grid[0] * fixture.grid[2])
	return {
		fixture.base_center.x + (f32(x) - 0.5 * f32(fixture.grid[0] - 1)) * fixture.spacing.x,
		fixture.base_center.y + f32(y) * fixture.spacing.y,
		fixture.base_center.z + (f32(z) - 0.5 * f32(fixture.grid[2] - 1)) * fixture.spacing.z,
	}
}

query_input :: proc(fixture: ^common.Spatial_Query, index, static_count: u32) -> Query_Input
{
	input: Query_Input
	local_index: u32 = index
	if index >= fixture.ray_count + fixture.sphere_cast_count
	{
		input.kind = .Overlap
		local_index -= fixture.ray_count + fixture.sphere_cast_count
	}
	else if index >= fixture.ray_count
	{
		input.kind = .Sphere_Cast
		local_index -= fixture.ray_count
	}
	sample: u32 = local_index / 2
	input.expected_hit = 1 - (local_index & 1)
	center: common.Vector3 = query_position(fixture, sample % static_count)
	first: common.Vector3 = query_position(fixture, 0)
	last: common.Vector3 = query_position(fixture, static_count - 1)
	minimum: [3]f32 = {first.x - fixture.half_extents.x, first.y - fixture.half_extents.y, first.z - fixture.half_extents.z}
	maximum: [3]f32 = {last.x + fixture.half_extents.x, last.y + fixture.half_extents.y, last.z + fixture.half_extents.z}
	origin: [3]f32 = {center.x, center.y, center.z}
	direction: [3]f32
	face: u32 = sample % 6
	axis: u32 = face / 2
	if input.kind == .Overlap
	{
		if input.expected_hit == 0
		{
			origin[axis] = maximum[axis] + fixture.miss_offset
		}
	}
	else
	{
		if (face & 1) == 0
		{
			origin[axis] = minimum[axis] - 5
			direction[axis] = 1
		}
		else
		{
			origin[axis] = maximum[axis] + 5
			direction[axis] = -1
		}
		if input.expected_hit == 0
		{
			transverse: u32 = 0
			if axis == 0
			{
				transverse = 1
			}
			origin[transverse] = maximum[transverse] + fixture.miss_offset
		}
	}
	input.origin = {origin[0], origin[1], origin[2]}
	input.direction = {direction[0], direction[1], direction[2]}
	return input
}

Query_Result :: struct
{
	hit: u32,
	distance: f32,
	status: physics.Physics_Status,
}

Query_Lane :: struct #align(128)
{
	status: common.Status,
	hits: u64,
}

Query_Phase :: enum u8
{
	Warmup,
	Measured,
}

Query_Outcome :: struct
{
	status: common.Status,
	phase: Query_Phase,
	batch: u32,
	family: Query_Kind,
	expected: u64,
	actual: u64,
}

Query_Runtime :: struct
{
	simulation: ^physics.Simulation,
	pool: ^util.Buffer_Pool,
	dispatcher: ^util.Thread_Dispatcher_Boundary,
	sphere: physics.Typed_Index,
	rays: []physics.Tree_Ray,
	sweeps: []Query_Input,
	overlaps: []util.Bounding_Box,
	debug_results: []Query_Result,
	lanes: [256]Query_Lane,
	threads: u32,
	distance: f32,
	active_kind: Query_Kind,
	last_hits: [3]u64,
	total_ms: [3]f64,
	outcome: Query_Outcome,
}

query_build :: proc(owner: ^Query_Runtime, world: ^common.Runtime, execution: ^common.Execution, recording_mode: common.Recording_Mode) -> common.Status
{
	shape: common.Native_Shape
	status: common.Status
	shape, status = common.native_shape(world, execution.geometry, execution.hull_points[:], 0)
	if status != .Ok
	{
		return status
	}
	for index: u32 = 0; index < execution.static_count; index += 1
	{
		status = common.add_static(world, shape, query_position(&execution.query, index))
		if status != .Ok
		{
			return status
		}
	}
	allocation: mem.Allocator_Error
	owner.rays, allocation = make([]physics.Tree_Ray, int(execution.query.ray_count))
	if allocation != nil
	{
		return .Capacity
	}
	owner.sweeps, allocation = make([]Query_Input, int(execution.query.sphere_cast_count))
	if allocation != nil
	{
		return .Capacity
	}
	owner.overlaps, allocation = make([]util.Bounding_Box, int(execution.query.overlap_count))
	if allocation != nil
	{
		return .Capacity
	}
	if recording_mode == .On
	{
		owner.debug_results, allocation = make([]Query_Result, int(execution.debug_count))
		if allocation != nil
		{
			return .Capacity
		}
	}
	owner.threads = world.threads
	owner.distance = execution.query.distance
	for index: u32 = 0; index < execution.query_count; index += 1
	{
		input: Query_Input = query_input(&execution.query, index, execution.static_count)
		switch input.kind
		{
		case .Ray:
			owner.rays[index] = {origin = input.origin, direction = input.direction, maximum_t = owner.distance}
		case .Sphere_Cast:
			owner.sweeps[index - execution.query.ray_count] = input
		case .Overlap:
			half: common.Vector3 = execution.query.overlap_half_extents
			owner.overlaps[index - execution.query.ray_count - execution.query.sphere_cast_count] = {
				min = {input.origin.x - half.x, input.origin.y - half.y, input.origin.z - half.z},
				max = {input.origin.x + half.x, input.origin.y + half.y, input.origin.z + half.z},
			}
		}
	}
	native_status: entasis.Status
	owner.sphere, native_status = entasis.shape_add(&world.world, entasis.sphere(execution.query.sphere_cast_radius))
	if native_status != .Ok
	{
		return .Physics_Failed
	}
	owner.simulation, native_status = entasis.world_borrow_simulation(&world.world)
	if native_status != .Ok
	{
		return .Physics_Failed
	}
	owner.pool, native_status = entasis.world_borrow_pool(&world.world)
	if native_status != .Ok
	{
		return .Physics_Failed
	}
	owner.dispatcher, native_status = entasis.world_borrow_dispatcher(&world.world)
	if native_status != .Ok || (owner.threads == 1 && owner.dispatcher != nil) ||
		(owner.threads > 1 && (owner.dispatcher == nil || owner.dispatcher.worker_count != int(owner.threads)))
	{
		return .Physics_Failed
	}
	return .Ok
}

query_destroy :: proc(owner: ^Query_Runtime)
{
	delete(owner.rays)
	delete(owner.sweeps)
	delete(owner.overlaps)
	delete(owner.debug_results)
	owner^ = {}
}

query_execute :: proc(owner: ^Query_Runtime, kind: Query_Kind, index: u32, pool: ^util.Buffer_Pool) -> Query_Result
{
	result: Query_Result
	switch kind
	{
	case .Ray:
		hit: physics.Ray_Query_Hit
		collector: physics.Ray_Query_Collector
		result.status = physics.ray_query_collector_initialize(&collector,
			{memory = cast([^]physics.Ray_Query_Hit)&hit, length = 1, id = util.BUFFER_CALLER_OWNED_ID}, .Earliest)
		if result.status == .Ok
		{
			result.status = physics.simulation_ray_query(owner.simulation, owner.rays[index], &collector, pool)
			result.hit = u32(collector.count > 0)
			result.distance = hit.t
		}
	case .Sphere_Cast:
		hit: physics.Sweep_Query_Hit
		collector: physics.Sweep_Query_Collector = {
			hits = {memory = cast([^]physics.Sweep_Query_Hit)&hit, length = 1, id = util.BUFFER_CALLER_OWNED_ID}, mode = .Earliest,
		}
		input: Query_Input = owner.sweeps[index]
		result.status = physics.simulation_sweep_query(owner.simulation, owner.sphere,
			entasis.pose(input.origin), entasis.velocity(input.direction), owner.distance, 0.05, 0.000005, 25, &collector, pool)
		result.hit = u32(collector.count > 0)
		result.distance = hit.sweep.t0
	case .Overlap:
		reference: physics.Collidable_Reference
		state: physics.Reference_State
		reference, state, result.status = physics.broad_phase_volume_any_query(&owner.simulation.broad_phase, owner.overlaps[index])
		result.hit = u32(state == .Present)
	}
	return result
}

query_lane :: proc(owner: ^Query_Runtime, lane: u32, pool: ^util.Buffer_Pool) -> Query_Lane
{
	count: u32
	switch owner.active_kind
	{
	case .Ray:
		count = u32(len(owner.rays))
	case .Sphere_Cast:
		count = u32(len(owner.sweeps))
	case .Overlap:
		count = u32(len(owner.overlaps))
	}
	range: Query_Range = query_range(count, lane, owner.threads)
	total: Query_Lane
	for index: u32 = range.begin; index < range.end; index += 1
	{
		result: Query_Result = query_execute(owner, owner.active_kind, index, pool)
		if result.status != .Ok
		{
			total.status = .Physics_Failed
			return total
		}
		total.hits += u64(result.hit)
	}
	return total
}

query_worker :: proc "contextless" (worker_index: int, dispatcher: ^util.Thread_Dispatcher_Boundary)
{
	context = runtime.default_context()
	owner: ^Query_Runtime = cast(^Query_Runtime)dispatcher.unmanaged_context
	pool: ^util.Buffer_Pool
	status: util.Threading_Status
	pool, status = dispatcher.worker_pool(dispatcher, worker_index)
	if status != .Ok
	{
		owner.lanes[worker_index].status = .Physics_Failed
		return
	}
	owner.lanes[worker_index] = query_lane(owner, u32(worker_index), pool)
}

query_reduce_hits :: proc(lanes: []Query_Lane) -> (u64, common.Status)
{
	hits: u64
	for lane in lanes
	{
		if lane.status != .Ok
		{
			return hits, lane.status
		}
		hits += lane.hits
	}
	return hits, .Ok
}

query_batch_outcome :: proc(owner: ^Query_Runtime, phase: Query_Phase, batch: u32) -> common.Status
{
	if owner.outcome.status != .Ok
	{
		return owner.outcome.status
	}
	counts: [3]u64 = {u64(len(owner.rays)), u64(len(owner.sweeps)), u64(len(owner.overlaps))}
	for count, family in counts
	{
		expected: u64 = count / 2 + count % 2
		if owner.last_hits[family] != expected
		{
			owner.outcome = {.Physics_Failed, phase, batch, Query_Kind(family), expected, owner.last_hits[family]}
			return .Physics_Failed
		}
	}
	return .Ok
}

query_measurement_reset :: proc(owner: ^Query_Runtime) -> common.Status
{
	if owner.outcome.status != .Ok
	{
		return owner.outcome.status
	}
	owner.last_hits = {}
	owner.total_ms = {}
	return .Ok
}

query_capture_debug :: proc(owner: ^Query_Runtime) -> common.Status
{
	samples: u32 = u32(len(owner.debug_results) / 3)
	for family: u32 = 0; family < 3; family += 1
	{
		for index: u32 = 0; index < samples; index += 1
		{
			result: Query_Result = query_execute(owner, Query_Kind(family), index, owner.pool)
			if result.status != .Ok
			{
				return .Physics_Failed
			}
			owner.debug_results[family * samples + index] = result
		}
	}
	return .Ok
}

query_batch :: proc(owner: ^Query_Runtime, phase: Query_Phase, batch: u32) -> (f64, common.Status)
{
	if owner.outcome.status != .Ok
	{
		return 0, owner.outcome.status
	}
	batch_start: time.Tick = time.tick_now()
	for family in 0..<3
	{
		start: time.Tick = time.tick_now()
		owner.active_kind = Query_Kind(family)
		for lane: u32 = 0; lane < owner.threads; lane += 1
		{
			owner.lanes[lane] = {status = .Invalid}
		}
		if owner.threads == 1
		{
			owner.lanes[0] = query_lane(owner, 0, owner.pool)
		}
		else if owner.dispatcher.dispatch(owner.dispatcher, query_worker, int(owner.threads), owner) != .Ok
		{
			return 0, .Physics_Failed
		}
		status: common.Status
		owner.last_hits[family], status = query_reduce_hits(owner.lanes[:owner.threads])
		if status != .Ok
		{
			return 0, status
		}
		milliseconds: f64 = f64(time.tick_since(start)) / f64(time.Millisecond)
		owner.total_ms[family] += milliseconds
	}
	elapsed: f64 = f64(time.tick_since(batch_start)) / f64(time.Millisecond)
	status: common.Status = query_batch_outcome(owner, phase, batch)
	if status != .Ok
	{
		fmt.eprintf("query_batch_failed engine=entasis phase=%v batch=%d family=%v expected=%d actual=%d\n",
			owner.outcome.phase, owner.outcome.batch, owner.outcome.family, owner.outcome.expected, owner.outcome.actual)
	}
	return elapsed, status
}
