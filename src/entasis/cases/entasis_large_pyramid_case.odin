package cases

import "polygon:common"
import entasis "entasis:entasis"

pyramid_position :: proc(fixture: ^common.Pyramid, index: u32) -> common.Vector3
{
	remaining: u32 = index
	for layer: u32 = 0; layer < fixture.rows; layer += 1
	{
		side: u32 = fixture.rows - layer
		if remaining < side * side
		{
			return {
				fixture.base_center.x + (f32(remaining % side) - 0.5 * f32(side - 1)) * fixture.spacing.x,
				fixture.base_center.y + f32(layer) * fixture.spacing.y,
				fixture.base_center.z + (f32(remaining / side) - 0.5 * f32(side - 1)) * fixture.spacing.z,
			}
		}
		remaining -= side * side
	}
	return {
		fixture.projectile_center.x + f32(remaining) * fixture.projectile_spacing.x,
		fixture.projectile_center.y + f32(remaining) * fixture.projectile_spacing.y,
		fixture.projectile_center.z + f32(remaining) * fixture.projectile_spacing.z,
	}
}

pyramid_build :: proc(runtime: ^common.Runtime, execution: ^common.Execution) -> common.Status
{
	fixture: ^common.Pyramid = &execution.pyramid
	shape: common.Native_Shape
	status: common.Status
	shape, status = common.native_shape(runtime, execution.geometry, nil, fixture.density)
	if status != .Ok
	{
		return status
	}
	box_count: u32 = execution.dynamic_count - fixture.projectile_count
	for index: u32 = 0; index < execution.dynamic_count; index += 1
	{
		if index == box_count
		{
			shape, status = common.native_shape(runtime, {shape = .Sphere, radius = fixture.projectile_radius}, nil, fixture.projectile_density)
			if status != .Ok
			{
				return status
			}
		}
		status = common.add_dynamic(runtime, shape, pyramid_position(fixture, index), execution.sleep_mode, execution.ccd_mode)
		if status != .Ok
		{
			return status
		}
	}
	shape, status = common.native_shape(runtime, {shape = .Box, half_extents = fixture.floor_half_extents}, nil, 0)
	if status != .Ok
	{
		return status
	}
	return common.add_static(runtime, shape, {0, -fixture.floor_half_extents.y, 0})
}

Pyramid_Launch :: enum u8
{
	None,
	Launch,
}

pyramid_launch_boundary :: proc(fixture: ^common.Pyramid, completed: u32) -> Pyramid_Launch
{
	if completed == fixture.launch_after
	{
		return .Launch
	}
	return .None
}

pyramid_launch_velocity :: proc(velocity: entasis.Body_Velocity, linear: common.Vector3) -> entasis.Body_Velocity
{
	result: entasis.Body_Velocity = velocity
	result.linear = linear
	return result
}

pyramid_prepare_step :: proc(runtime: ^common.Runtime, execution: ^common.Execution, completed: u32) -> common.Status
{
	if pyramid_launch_boundary(&execution.pyramid, completed) == .Launch
	{
		for index: u32 = execution.dynamic_count - execution.pyramid.projectile_count; index < execution.dynamic_count; index += 1
		{
			body: entasis.Body_State
			status: entasis.Status
			body, status = entasis.body_get(&runtime.world, runtime.bodies[index])
			if status != .Ok
			{
				return .Physics_Failed
			}
			velocity: entasis.Body_Velocity = pyramid_launch_velocity(body.velocity, execution.pyramid.projectile_velocity)
			if entasis.body_set_velocity(&runtime.world, runtime.bodies[index], velocity) != .Ok
			{
				return .Physics_Failed
			}
		}
	}
	return .Ok
}
