package common

import "core:math"

Visual_Geometry :: struct
{
	kind: u32,
	parameters: Vector3,
	vertex_count: u32,
	index_count: u32,
	edge_count: u32,
}

Visual_Mesh :: struct
{
	vertices: [130]Vector3,
	indices: [768]u32,
	edges: [272][2]u32,
	vertex_count: u32,
	index_count: u32,
	edge_count: u32,
}

capsule_mesh :: proc(radius, half_segment: f32, mesh: ^Visual_Mesh)
{
	mesh.vertex_count = 130
	mesh.vertices[0] = {0, half_segment + radius, 0}
	mesh.vertices[129] = {0, -half_segment - radius, 0}
	for ring: u32 = 0; ring < 8; ring += 1
	{
		latitude: f32 = f32(ring) * math.PI / 8
		center_y: f32 = -half_segment
		if ring < 4
		{
			latitude = f32(ring + 1) * math.PI / 8
			center_y = half_segment
		}
		y: f32 = center_y + radius * math.cos(latitude)
		radial: f32 = radius * math.sin(latitude)
		for longitude: u32 = 0; longitude < 16; longitude += 1
		{
			angle: f32 = f32(longitude) * 2 * math.PI / 16
			mesh.vertices[1 + ring * 16 + longitude] = {radial * math.cos(angle), y, radial * math.sin(angle)}
		}
	}
	for longitude: u32 = 0; longitude < 16; longitude += 1
	{
		next: u32 = (longitude + 1) % 16
		triangle: [3]u32 = {0, 1 + next, 1 + longitude}
		copy(mesh.indices[mesh.index_count:], triangle[:])
		mesh.index_count += 3
		mesh.edges[mesh.edge_count] = {0, 1 + longitude}
		mesh.edge_count += 1
		for ring: u32 = 0; ring < 8; ring += 1
		{
			current: u32 = 1 + ring * 16
			mesh.edges[mesh.edge_count] = {current + longitude, current + next}
			mesh.edge_count += 1
			if ring + 1 < 8
			{
				lower: u32 = current + 16
				triangles: [6]u32 = {current + longitude, current + next, lower + longitude, current + next, lower + next, lower + longitude}
				copy(mesh.indices[mesh.index_count:], triangles[:])
				mesh.index_count += 6
				mesh.edges[mesh.edge_count] = {current + longitude, lower + longitude}
				mesh.edge_count += 1
			}
		}
		last: u32 = 113
		triangle = {last + longitude, last + next, 129}
		copy(mesh.indices[mesh.index_count:], triangle[:])
		mesh.index_count += 3
		mesh.edges[mesh.edge_count] = {last + longitude, 129}
		mesh.edge_count += 1
	}
}

hull_mesh :: proc(points: []Vector3, half: Vector3, mesh: ^Visual_Mesh) -> Status
{
	mesh.vertex_count = 24
	for point, index in points
	{
		mesh.vertices[index] = {point.x * half.x, point.y * half.y, point.z * half.z}
	}
	for face: u32 = 0; face < 14; face += 1
	{
		corners: [8]u32
		count: u32
		if face < 6
		{
			axis: u32 = face / 2
			sign: f32 = -1
			if (face & 1) != 0
			{
				sign = 1
			}
			angles: [8]f32
			for point, index in points
			{
				components: [3]f32 = {point.x, point.y, point.z}
				if components[axis] != sign
				{
					continue
				}
				if count == 8
				{
					return .Invalid
				}
				angle: f32 = sign * math.atan2(components[(axis + 2) % 3], components[(axis + 1) % 3])
				insertion: u32 = count
				count += 1
				for insertion != 0 && angles[insertion - 1] > angle
				{
					corners[insertion] = corners[insertion - 1]
					angles[insertion] = angles[insertion - 1]
					insertion -= 1
				}
				corners[insertion] = u32(index)
				angles[insertion] = angle
			}
			if count != 8
			{
				return .Invalid
			}
		}
		else
		{
			signs: u32 = face - 6
			positive_count: u32 = (signs & 1) + ((signs >> 1) & 1) + ((signs >> 2) & 1)
			corners[0] = signs
			corners[1] = 16 + signs
			corners[2] = 8 + signs
			if (positive_count & 1) != 0
			{
				corners[1], corners[2] = corners[2], corners[1]
			}
			count = 3
		}
		for corner: u32 = 1; corner + 1 < count; corner += 1
		{
			triangle: [3]u32 = {corners[0], corners[corner], corners[corner + 1]}
			copy(mesh.indices[mesh.index_count:], triangle[:])
			mesh.index_count += 3
		}
		for corner: u32 = 0; corner < count; corner += 1
		{
			a: u32 = min(corners[corner], corners[(corner + 1) % count])
			b: u32 = max(corners[corner], corners[(corner + 1) % count])
			edge: u32
			for edge < mesh.edge_count && (mesh.edges[edge][0] != a || mesh.edges[edge][1] != b)
			{
				edge += 1
			}
			if edge != mesh.edge_count
			{
				continue
			}
			if mesh.edge_count == 36
			{
				return .Invalid
			}
			mesh.edges[mesh.edge_count] = {a, b}
			mesh.edge_count += 1
		}
	}
	if mesh.index_count != 132 || mesh.edge_count != 36
	{
		return .Invalid
	}
	return .Ok
}

visual_geometry :: proc(geometry: Geometry, points: []Vector3, mesh: ^Visual_Mesh) -> (Visual_Geometry, Status)
{
	switch geometry.shape
	{
	case .Box:
		return {kind = 2, parameters = geometry.half_extents}, .Ok
	case .Sphere:
		return {kind = 1, parameters = {geometry.radius, 0, 0}}, .Ok
	case .Capsule:
		capsule_mesh(geometry.radius, geometry.half_segment, mesh)
	case .Convex_Hull:
		if hull_mesh(points, geometry.half_extents, mesh) != .Ok
		{
			return {}, .Invalid
		}
	}
	return {kind = 3, vertex_count = mesh.vertex_count, index_count = mesh.index_count, edge_count = mesh.edge_count}, .Ok
}
