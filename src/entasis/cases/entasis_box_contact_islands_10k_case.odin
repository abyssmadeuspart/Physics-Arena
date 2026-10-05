package cases

import "polygon:common"

islands_floor_position :: proc(fixture: ^common.Contact_Islands, index: u32) -> common.Vector3
{
	x: u32 = index % fixture.island_grid[0]
	z: u32 = index / fixture.island_grid[0]
	return {
		(f32(x) - 0.5 * f32(fixture.island_grid[0] - 1)) * fixture.island_spacing[0],
		-fixture.floor_half_extents.y,
		(f32(z) - 0.5 * f32(fixture.island_grid[1] - 1)) * fixture.island_spacing[1],
	}
}

islands_body_position :: proc(fixture: ^common.Contact_Islands, index: u32) -> common.Vector3
{
	bodies_per_island: u32 = fixture.body_grid[0] * fixture.body_grid[1] * fixture.body_grid[2]
	origin: common.Vector3 = islands_floor_position(fixture, index / bodies_per_island)
	local: u32 = index % bodies_per_island
	x: u32 = local % fixture.body_grid[0]
	z: u32 = (local / fixture.body_grid[0]) % fixture.body_grid[2]
	y: u32 = local / (fixture.body_grid[0] * fixture.body_grid[2])
	return {
		origin.x + (f32(x) - 0.5 * f32(fixture.body_grid[0] - 1)) * fixture.body_spacing.x,
		fixture.initial_y + f32(y) * fixture.body_spacing.y,
		origin.z + (f32(z) - 0.5 * f32(fixture.body_grid[2] - 1)) * fixture.body_spacing.z,
	}
}

islands_build :: proc(runtime: ^common.Runtime, execution: ^common.Execution) -> common.Status
{
	shape: common.Native_Shape
	status: common.Status
	shape, status = common.native_shape(runtime,
		{shape = .Box, half_extents = execution.islands.floor_half_extents}, nil, 0)
	if status != .Ok
	{
		return status
	}
	for index: u32 = 0; index < execution.static_count; index += 1
	{
		status = common.add_static(runtime, shape, islands_floor_position(&execution.islands, index))
		if status != .Ok
		{
			return status
		}
	}
	shape, status = common.native_shape(runtime, execution.geometry, execution.hull_points[:], execution.islands.density)
	if status != .Ok
	{
		return status
	}
	for index: u32 = 0; index < execution.dynamic_count; index += 1
	{
		status = common.add_dynamic(runtime, shape, islands_body_position(&execution.islands, index), execution.sleep_mode, execution.ccd_mode)
		if status != .Ok
		{
			return status
		}
	}
	return .Ok
}
