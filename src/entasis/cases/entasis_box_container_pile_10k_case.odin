package cases

import "polygon:common"

container_position :: proc(fixture: ^common.Container, index: u32) -> common.Vector3
{
	x: u32 = index % fixture.grid[0]
	z: u32 = (index / fixture.grid[0]) % fixture.grid[2]
	y: u32 = index / (fixture.grid[0] * fixture.grid[2])
	return {
		(f32(x) - 0.5 * f32(fixture.grid[0] - 1)) * fixture.spacing.x,
		fixture.initial_y + f32(y) * fixture.spacing.y,
		(f32(z) - 0.5 * f32(fixture.grid[2] - 1)) * fixture.spacing.z,
	}
}

container_build :: proc(runtime: ^common.Runtime, execution: ^common.Execution) -> common.Status
{
	shape: common.Native_Shape
	status: common.Status
	shape, status = common.native_shape(runtime, execution.geometry, execution.hull_points[:], execution.container.density)
	if status != .Ok
	{
		return status
	}
	for index: u32 = 0; index < execution.dynamic_count; index += 1
	{
		status = common.add_dynamic(runtime, shape, container_position(&execution.container, index), execution.sleep_mode, execution.ccd_mode)
		if status != .Ok
		{
			return status
		}
	}
	for box in execution.container.static_boxes[:execution.container.static_count]
	{
		shape, status = common.native_shape(runtime, {shape = .Box, half_extents = box.half_extents}, nil, 0)
		if status != .Ok
		{
			return status
		}
		status = common.add_static(runtime, shape, box.center)
		if status != .Ok
		{
			return status
		}
	}
	return .Ok
}
