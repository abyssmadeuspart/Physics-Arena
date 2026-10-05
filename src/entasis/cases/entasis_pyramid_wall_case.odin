package cases

import "core:math"
import "core:time"
import "polygon:common"
import entasis "entasis:entasis"

Wall_Observation :: struct
{
	height: f64,
	lateral_rms: f64,
	translational_energy: f64,
	rotational_energy: f64,
	potential_energy: f64,
	penetration: f64,
	escaped: u64,
	invalid: u64,
	elapsed_ms: f64,
}

Wall_Observations :: struct
{
	samples: [4]Wall_Observation,
	inputs: [4][]entasis.Body_State,
	initial_potential_energy: f64,
}

pyramid_wall_position :: proc(wall: ^common.Pyramid_Wall, index: u32) -> common.Vector3
{
	remaining: u32 = index
	row: u32
	width: u32 = wall.rows
	for remaining >= width
	{
		remaining -= width
		width -= 1
		row += 1
	}
	h: f32 = wall.half_extent
	return {(f32(row) + 1) * h + 2 * f32(remaining) * h - h * f32(wall.rows), (2 * f32(row) + 1) * h, 0}
}

pyramid_wall_build :: proc(runtime: ^common.Runtime, execution: ^common.Execution, observations: ^Wall_Observations) -> common.Status
{
	w: ^common.Pyramid_Wall = &execution.pyramid_wall
	shape: common.Native_Shape
	status: common.Status
	shape, status = common.native_shape(runtime, execution.geometry, nil, w.density)
	if status != .Ok
	{
		return status
	}
	mass: f32 = 8 * w.half_extent * w.half_extent * w.half_extent * w.density
	inverse_mass: f32 = 1 / mass
	inverse_inertia: f32 = 1.5 / (mass * w.half_extent * w.half_extent)
	for index: u32 = 0; index < execution.dynamic_count; index += 1
	{
		position: common.Vector3 = pyramid_wall_position(w, index)
		status = common.add_dynamic(runtime, shape, position, execution.sleep_mode, execution.ccd_mode)
		if status != .Ok
		{
			return status
		}
		body: entasis.Body_State
		native_status: entasis.Status
		body, native_status = entasis.body_get(&runtime.world, runtime.bodies[index])
		if native_status != .Ok || body.pose.position != position || body.pose.orientation != (common.Quaternion{w = 1}) ||
			body.velocity != (entasis.Body_Velocity{}) ||
			(body.activity.sleep_threshold >= 0) != (execution.sleep_mode == 1) ||
			math.abs(body.local_inertia.inverse_mass - inverse_mass) > 1e-5 * inverse_mass ||
			math.abs(body.local_inertia.inverse_inertia_tensor.xx - inverse_inertia) > 1e-5 * inverse_inertia ||
			math.abs(body.local_inertia.inverse_inertia_tensor.yy - inverse_inertia) > 1e-5 * inverse_inertia ||
			math.abs(body.local_inertia.inverse_inertia_tensor.zz - inverse_inertia) > 1e-5 * inverse_inertia ||
			body.local_inertia.inverse_inertia_tensor.yx != 0 || body.local_inertia.inverse_inertia_tensor.zx != 0 ||
			body.local_inertia.inverse_inertia_tensor.zy != 0
		{
			return .Physics_Failed
		}
		if observations != nil
		{
			observations.initial_potential_energy -= (f64(execution.gravity.x) * f64(position.x) +
				f64(execution.gravity.y) * f64(position.y) + f64(execution.gravity.z) * f64(position.z)) / f64(body.local_inertia.inverse_mass)
		}
	}
	shape, status = common.native_shape(runtime, {shape = .Box, half_extents = w.floor_half_extents}, nil, 0)
	if status != .Ok
	{
		return status
	}
	return common.add_static(runtime, shape, {0, -w.floor_half_extents.y, 0})
}

pyramid_wall_observation_step :: proc(measured, ordinal: u32) -> u32
{
	if measured < 4 || ordinal == 0
	{
		return ordinal + 1
	}
	if ordinal == 3
	{
		return measured
	}
	return max(ordinal + 1, (measured + 1) / (4 if ordinal == 1 else 2))
}

pyramid_wall_observation_index :: proc(completed, measured: u32) -> int
{
	for index: u32 = 0; index < min(4, measured); index += 1
	{
		if completed == pyramid_wall_observation_step(measured, index)
		{
			return int(index)
		}
	}
	return -1
}

pyramid_wall_capture :: proc(runtime: ^common.Runtime, inputs: []entasis.Body_State, sample: ^Wall_Observation) -> common.Status
{
	start: time.Tick = time.tick_now()
	for index in 0..<len(inputs)
	{
		status: entasis.Status
		inputs[index], status = entasis.body_get(&runtime.world, runtime.bodies[index])
		if status != .Ok
		{
			return .Physics_Failed
		}
	}
	sample.elapsed_ms = f64(time.tick_since(start)) / f64(time.Millisecond)
	return .Ok
}

pyramid_wall_reduce :: proc(inputs: []entasis.Body_State, execution: ^common.Execution, sample: ^Wall_Observation) -> common.Status
{
	start: time.Tick = time.tick_now()
	w: ^common.Pyramid_Wall = &execution.pyramid_wall
	for index: u32 = 0; index < execution.dynamic_count; index += 1
	{
		body: entasis.Body_State = inputs[index]
		p: common.Vector3 = body.pose.position
		q: common.Quaternion = body.pose.orientation
		v: common.Vector3 = body.velocity.linear
		a: common.Vector3 = body.velocity.angular
		values: [13]f32 = {p.x, p.y, p.z, q.x, q.y, q.z, q.w, v.x, v.y, v.z, a.x, a.y, a.z}
		invalid: u32
		for value in values
		{
			if math.is_nan(value) || math.is_inf(value)
			{
				invalid = 1
			}
		}
		if invalid != 0
		{
			sample.invalid += 1
			continue
		}
		if (body.activity.sleep_threshold >= 0) != (execution.sleep_mode == 1)
		{
			sample.invalid += 1
		}
		initial: common.Vector3 = pyramid_wall_position(w, index)
		dx: f64 = f64(p.x) - f64(initial.x)
		dz: f64 = f64(p.z) - f64(initial.z)
		mass: f64 = 1 / f64(body.local_inertia.inverse_mass)
		sample.height += f64(p.y)
		sample.lateral_rms += dx * dx + dz * dz
		sample.translational_energy += 0.5 * mass * (f64(v.x) * f64(v.x) + f64(v.y) * f64(v.y) + f64(v.z) * f64(v.z))
		sample.rotational_energy += 0.5 * (f64(a.x) * f64(a.x) + f64(a.y) * f64(a.y) + f64(a.z) * f64(a.z)) / f64(body.local_inertia.inverse_inertia_tensor.xx)
		sample.potential_energy -= mass * (f64(execution.gravity.x) * f64(p.x) +
			f64(execution.gravity.y) * f64(p.y) + f64(execution.gravity.z) * f64(p.z))
		x: f64 = f64(q.x)
		y: f64 = f64(q.y)
		z: f64 = f64(q.z)
		qw: f64 = f64(q.w)
		h: f64 = f64(w.half_extent)
		sx: f64 = h * (math.abs(1 - 2 * (y * y + z * z)) + math.abs(2 * (x * y - z * qw)) + math.abs(2 * (x * z + y * qw)))
		sy: f64 = h * (math.abs(2 * (x * y + z * qw)) + math.abs(1 - 2 * (x * x + z * z)) + math.abs(2 * (y * z - x * qw)))
		sz: f64 = h * (math.abs(2 * (x * z - y * qw)) + math.abs(2 * (y * z + x * qw)) + math.abs(1 - 2 * (x * x + y * y)))
		sample.penetration = max(sample.penetration, sy - f64(p.y))
		if math.abs(f64(p.x)) + sx > f64(w.floor_half_extents.x) || math.abs(f64(p.z)) + sz > f64(w.floor_half_extents.z)
		{
			sample.escaped += 1
		}
	}
	sample.height /= f64(execution.dynamic_count)
	sample.lateral_rms = math.sqrt(sample.lateral_rms / f64(execution.dynamic_count))
	sample.elapsed_ms += f64(time.tick_since(start)) / f64(time.Millisecond)
	return .Ok
}
