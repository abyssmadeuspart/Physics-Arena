package common

Recording_Mode :: enum u8
{
	Off,
	On,
}

import "core:fmt"
import "core:mem"
import "core:os"
import "core:strings"

Recording_Layout :: struct
{
	scene_bytes: u64,
	frames_offset: u64,
	frame_stride: u64,
	frame_count: u64,
	trailer_offset: u64,
	file_bytes: u64,
}

Recording_State :: enum u8
{
	Closed,
	Open,
	Failed,
	Complete,
}

Recording_Writer :: struct
{
	file: ^os.File,
	final_path: string,
	partial_path: string,
	frame: []u8,
	layout: Recording_Layout,
	written_frames: u64,
	state: Recording_State,
}

recording_layout :: proc(e: ^Execution, geometry_count: u32, mesh: ^Visual_Mesh) -> (Recording_Layout, Status)
{
	dynamic_capacity: u32 = 16210
	if e.family == .Pyramid_Wall
	{
		dynamic_capacity = 16290
	}
	if geometry_count == 0 || geometry_count > 64 || e.instance_count == 0 || e.instance_count > dynamic_capacity + 1 ||
		e.dynamic_count > dynamic_capacity || e.debug_count > 768 || e.measured_count == 0 || e.measured_count > 100000
	{
		return {}, .Invalid
	}
	layout: Recording_Layout
	layout.scene_bytes = 120 + u64(geometry_count) * 40 + u64(e.instance_count) * 44 +
		u64(mesh.vertex_count) * 12 + u64(mesh.index_count) * 4 + u64(mesh.edge_count) * 8
	layout.frames_offset = 384 + layout.scene_bytes
	layout.frame_stride = 8 + u64(e.dynamic_count) * 32 + u64(e.debug_count) * 40
	layout.frame_count = u64(e.measured_count) + 1
	if layout.frame_count > (max(u64) - layout.frames_offset - 24) / layout.frame_stride
	{
		return {}, .Capacity
	}
	layout.trailer_offset = layout.frames_offset + layout.frame_count * layout.frame_stride
	layout.file_bytes = layout.trailer_offset + 24
	return layout, .Ok
}

recording_header :: proc(writer: ^Writer, e: ^Execution, threads, repeat, geometry_count: u32,
	mesh: ^Visual_Mesh, layout: Recording_Layout)
{
	for value in "BPREPLAY"
	{
		write_u8(writer, u8(value))
	}
	write_u32(writer, 1)
	write_u32(writer, 384)
	write_fixed_text(writer, e.case_id, 64)
	write_fixed_text(writer, "entasis", 64)
	write_fixed_text(writer, e.semantic, 64)
	revision: [16]u8
	write_fixed_text(writer, fmt.bprintf(revision[:], "%d", e.revision), 64)
	for field in ([6]u32{threads, repeat, e.measured_count, e.warmup_count, e.body_count, e.shape_count})
	{
		write_u32(writer, field)
	}
	kind: u32 = 1
	timestep: f64
	if e.family == .Query
	{
		kind = 2
	}
	else
	{
		timestep = 1.0 / f64(e.timestep_hz)
	}
	write_u32(writer, kind)
	write_f64(writer, timestep)
	for field in ([7]u32{geometry_count, e.instance_count, e.dynamic_count, e.debug_count,
		mesh.vertex_count, mesh.index_count, mesh.edge_count})
	{
		write_u32(writer, field)
	}
	for field in ([6]u64{384, layout.scene_bytes, layout.frames_offset, layout.frame_stride, layout.frame_count, layout.trailer_offset})
	{
		write_u64(writer, field)
	}
	camera: Replay_Camera = e.replay_camera
	for value in ([8]Vector3{camera.direction, camera.up, camera.minimum, camera.maximum,
		camera.eye, camera.target, camera.eye_offset, camera.target_offset})
	{
		write_vector(writer, value)
	}
	for value in ([4]f32{camera.vertical_fov_degrees, camera.viewport_fill, camera.near_plane, camera.far_plane})
	{
		write_f32(writer, value)
	}
	write_u32(writer, camera.stable_slot)
	write_u32(writer, camera.mode)
}

recording_begin :: proc(writer: ^Recording_Writer, path: string, scene: []u8, layout: Recording_Layout) -> Status
{
	if !strings.has_suffix(path, ".bpr") || os.exists(path) || len(scene) != int(layout.frames_offset)
	{
		return .Invalid
	}
	writer.layout = layout
	writer.final_path = path
	writer.partial_path = fmt.aprintf("%s.partial", path)
	allocation: mem.Allocator_Error
	writer.frame, allocation = make([]u8, int(layout.frame_stride))
	if allocation != nil
	{
		return .Capacity
	}
	error: os.Error
	writer.file, error = os.open(writer.partial_path, {.Write, .Create, .Excl})
	if error != nil
	{
		return .Io_Failed
	}
	count: int
	count, error = os.write(writer.file, scene)
	if error != nil || count != len(scene)
	{
		writer.state = .Failed
		return .Io_Failed
	}
	writer.state = .Open
	return .Ok
}

recording_append :: proc(writer: ^Recording_Writer, ordinal: u64, size: int) -> Status
{
	if writer.state != .Open || ordinal != writer.written_frames || ordinal >= writer.layout.frame_count || size != len(writer.frame)
	{
		writer.state = .Failed
		return .Invalid
	}
	writer.state = .Failed
	count: int
	error: os.Error
	count, error = os.write(writer.file, writer.frame)
	if error != nil || count != size
	{
		return .Io_Failed
	}
	writer.written_frames += 1
	writer.state = .Open
	return .Ok
}

recording_complete :: proc(writer: ^Recording_Writer) -> Status
{
	if writer.state != .Open || writer.written_frames != writer.layout.frame_count
	{
		return .Invalid
	}
	writer.state = .Failed
	trailer: [24]u8
	copy(trailer[:8], "BPRDONE1")
	output: Writer = {bytes = trailer[:], offset = 8}
	write_u64(&output, writer.written_frames)
	write_u64(&output, writer.layout.file_bytes)
	count: int
	error: os.Error
	count, error = os.write(writer.file, trailer[:])
	flush_error: os.Error = os.flush(writer.file)
	close_error: os.Error = os.close(writer.file)
	writer.file = nil
	if error != nil || count != len(trailer) || flush_error != nil || close_error != nil ||
		os.exists(writer.final_path) || os.rename(writer.partial_path, writer.final_path) != nil
	{
		return .Io_Failed
	}
	writer.state = .Complete
	return .Ok
}

recording_destroy :: proc(writer: ^Recording_Writer) -> Status
{
	error: os.Error
	if writer.file != nil
	{
		error = os.close(writer.file)
	}
	delete(writer.frame)
	delete(writer.partial_path)
	writer^ = {}
	if error != nil
	{
		return .Io_Failed
	}
	return .Ok
}
