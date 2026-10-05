package main

import "core:mem"
import "polygon:common"
import "polygon:cases"
import entasis "entasis:entasis"

recording_scene :: proc(session: ^Session, arguments: ^Arguments) -> ([]u8, common.Recording_Layout, common.Status)
{
	e: ^common.Execution = session.execution
	mesh: common.Visual_Mesh
	geometries: [17]common.Visual_Geometry
	status: common.Status
	geometries[0], status = common.visual_geometry(e.geometry, e.hull_points[:], &mesh)
	if status != .Ok
	{
		return nil, {}, status
	}
	geometry_count: u32 = 1
	switch e.family
	{
	case .Container:
		geometry_count += e.static_count
		for box, index in e.container.static_boxes[:e.static_count]
		{
			geometries[index + 1] = {kind = 2, parameters = box.half_extents}
		}
	case .Contact_Islands:
		geometry_count = 2
		geometries[1] = {kind = 2, parameters = e.islands.floor_half_extents}
	case .Pyramid:
		geometry_count = 3
		geometries[1] = {kind = 1, parameters = {e.pyramid.projectile_radius, 0, 0}}
		geometries[2] = {kind = 2, parameters = e.pyramid.floor_half_extents}
	case .Pyramid_Wall:
		geometry_count = 2
		geometries[1] = {kind = 2, parameters = e.pyramid_wall.floor_half_extents}
	case .Query:
	case .Ray_Tracing:
		return nil, {}, .Invalid
	}
	layout: common.Recording_Layout
	layout, status = common.recording_layout(e, geometry_count, &mesh)
	if status != .Ok
	{
		return nil, {}, status
	}
	bytes: []u8
	allocation: mem.Allocator_Error
	bytes, allocation = make([]u8, int(layout.frames_offset))
	if allocation != nil
	{
		return nil, {}, .Capacity
	}
	writer: common.Writer = {bytes = bytes}
	common.recording_header(&writer, e, arguments.threads, arguments.repeat, geometry_count, &mesh, layout)
	for geometry in geometries[:geometry_count]
	{
		common.write_u32(&writer, geometry.kind)
		common.write_vector(&writer, geometry.parameters)
		common.write_u32(&writer, 0)
		common.write_u32(&writer, geometry.vertex_count)
		common.write_u32(&writer, 0)
		common.write_u32(&writer, geometry.index_count)
		common.write_u32(&writer, 0)
		common.write_u32(&writer, geometry.edge_count)
	}
	for index: u32 = 0; index < e.instance_count; index += 1
	{
		geometry: u32
		transform_slot: u32 = max(u32)
		pose: entasis.Rigid_Pose
		if index < e.dynamic_count
		{
			transform_slot = index
			pose, status = common.body_pose(&session.runtime, index)
			if status != .Ok
			{
				delete(bytes)
				return nil, {}, status
			}
			if e.family == .Pyramid && index >= e.dynamic_count - e.pyramid.projectile_count
			{
				geometry = 1
			}
		}
		else
		{
			pose.orientation = {w = 1}
			switch e.family
			{
			case .Container:
				geometry = index - e.dynamic_count + 1
				pose.position = e.container.static_boxes[index - e.dynamic_count].center
			case .Contact_Islands:
				geometry = 1
				pose.position = cases.islands_floor_position(&e.islands, index - e.dynamic_count)
			case .Pyramid:
				geometry = 2
				pose.position = {0, -e.pyramid.floor_half_extents.y, 0}
			case .Pyramid_Wall:
				geometry = 1
				pose.position = {0, -e.pyramid_wall.floor_half_extents.y, 0}
			case .Query:
				pose.position = cases.query_position(&e.query, index)
				pose.orientation = common.shape_rotation(e.geometry)
			case .Ray_Tracing:
				unreachable()
			}
		}
		common.write_u32(&writer, geometry)
		common.write_u32(&writer, index)
		common.write_u32(&writer, 0)
		common.write_u32(&writer, transform_slot)
		common.write_transform(&writer, pose.position, pose.orientation)
	}
	for vertex in mesh.vertices[:mesh.vertex_count]
	{
		common.write_vector(&writer, vertex)
	}
	for index in mesh.indices[:mesh.index_count]
	{
		common.write_u32(&writer, index)
	}
	for edge in mesh.edges[:mesh.edge_count]
	{
		common.write_u32(&writer, edge[0])
		common.write_u32(&writer, edge[1])
	}
	if writer.status != .Ok || writer.offset != len(bytes)
	{
		delete(bytes)
		return nil, {}, .Capacity
	}
	return bytes, layout, .Ok
}

recording_frame :: proc(session: ^Session, bytes: []u8) -> (int, common.Status)
{
	writer: common.Writer = {bytes = bytes}
	common.write_u64(&writer, u64(session.completed))
	for index: u32 = 0; index < session.execution.dynamic_count; index += 1
	{
		pose: entasis.Rigid_Pose
		status: common.Status
		pose, status = common.body_pose(&session.runtime, index)
		if status != .Ok
		{
			return 0, status
		}
		common.write_u32(&writer, index)
		common.write_transform(&writer, pose.position, pose.orientation)
	}
	if session.execution.family == .Query
	{
		status: common.Status = cases.query_capture_debug(&session.query)
		if status != .Ok
		{
			return 0, status
		}
		e: ^common.Execution = session.execution
		bases: [3]u32 = {0, e.query.ray_count, e.query.ray_count + e.query.sphere_cast_count}
		for family: u32 = 0; family < 3; family += 1
		{
			for local: u32 = 0; local < e.query.debug_samples; local += 1
			{
				index: u32 = bases[family] + local
				input: cases.Query_Input = cases.query_input(&e.query, index, e.static_count)
				result: cases.Query_Result = session.query.debug_results[family * e.query.debug_samples + local]
				material: u32 = 7
				distance: f32 = e.query.distance
				if result.hit != 0
				{
					material = 6
					distance = result.distance
				}
				end: common.Vector3 = {input.origin.x + input.direction.x * distance,
					input.origin.y + input.direction.y * distance, input.origin.z + input.direction.z * distance}
				radius: f32
				if family == 1
				{
					radius = e.query.sphere_cast_radius
				}
				if family == 2
				{
					end = e.query.overlap_half_extents
				}
				common.write_u32(&writer, family)
				common.write_u32(&writer, material)
				common.write_vector(&writer, input.origin)
				common.write_vector(&writer, end)
				common.write_f32(&writer, radius)
				common.write_u32(&writer, 0)
			}
		}
	}
	return writer.offset, writer.status
}
