package main

import "core:mem"
import "core:os"
import "core:time"
import "polygon:common"
import entasis "entasis:entasis"

Stack_Capture :: struct
{
	file: ^os.File,
	bytes: []u8,
	frames: u32,
	segment: u32,
	elapsed_ms: f64,
}

stack_capture_open :: proc(capture: ^Stack_Capture, arguments: ^Arguments, execution: ^common.Execution) -> common.Status
{
	if arguments.verification_mode == .Off
	{
		return .Ok
	}

	if execution.family != .Container && execution.family != .Contact_Islands && execution.family != .Pyramid && execution.family != .Pyramid_Wall
	{
		return .Ok
	}
	allocation: mem.Allocator_Error
	capture.bytes, allocation = make([]u8, 12 + int(execution.dynamic_count) * 32)
	if allocation != nil
	{
		return .Capacity
	}
	error: os.Error
	capture.file, error = os.open(arguments.stack_stream, {.Read, .Write})
	if error != nil
	{
		return .Io_Failed
	}
	header: [236]u8
	copy(header[:8], "BPSTACK")
	writer: common.Writer = {bytes = header[:], offset = 8}
	common.write_fixed_text(&writer, execution.case_id, 64)
	common.write_fixed_text(&writer, execution.semantic, 64)
	common.write_fixed_text(&writer, "entasis", 64)
	box_count: u32 = execution.dynamic_count
	if execution.family == .Pyramid
	{
		box_count -= execution.pyramid.projectile_count
	}
	fields: [9]u32 = {u32(execution.family), execution.revision, execution.dynamic_count, box_count,
		arguments.threads, arguments.repeat, execution.warmup_count, execution.measured_count, execution.timestep_hz}
	for field in fields
	{
		common.write_u32(&writer, field)
	}
	if writer.status != .Ok
	{
		return writer.status
	}
	return stack_capture_exchange(capture, header[:])
}

stack_capture_append :: proc(capture: ^Stack_Capture, runtime: ^common.Runtime, phase, step: u32) -> common.Status
{
	if capture.file == nil
	{
		return .Ok
	}
	start: time.Tick = time.tick_now()
	writer: common.Writer = {bytes = capture.bytes}
	common.write_u32(&writer, phase)
	common.write_u32(&writer, capture.segment)
	common.write_u32(&writer, step)
	for index: u32 = 0; index < u32(len(runtime.bodies)); index += 1
	{
		pose: entasis.Rigid_Pose
		status: common.Status
		pose, status = common.body_pose(runtime, index)
		if status != .Ok
		{
			return status
		}
		common.write_u32(&writer, index)
		common.write_vector(&writer, pose.position)
		common.write_f32(&writer, pose.orientation.x)
		common.write_f32(&writer, pose.orientation.y)
		common.write_f32(&writer, pose.orientation.z)
		common.write_f32(&writer, pose.orientation.w)
	}
	if writer.status != .Ok
	{
		return writer.status
	}
	status: common.Status = stack_capture_exchange(capture, capture.bytes)
	capture.elapsed_ms += f64(time.tick_since(start)) / f64(time.Millisecond)
	if status != .Ok
	{
		return status
	}
	capture.frames += 1
	return .Ok
}

stack_capture_close :: proc(capture: ^Stack_Capture) -> common.Status
{
	if capture.file == nil
	{
		return .Ok
	}
	footer: [16]u8
	writer: common.Writer = {bytes = footer[:]}
	common.write_u32(&writer, 3)
	common.write_u32(&writer, capture.frames)
	common.write_f64(&writer, capture.elapsed_ms)
	status: common.Status = stack_capture_exchange(capture, footer[:])
	error: os.Error = os.close(capture.file)
	capture.file = nil
	return status if error == nil else .Io_Failed
}

stack_capture_destroy :: proc(capture: ^Stack_Capture)
{
	if capture.file != nil
	{
		os.close(capture.file)
	}
	delete(capture.bytes)
	capture^ = {}
}

Stack_Reply :: enum u32
{
	Accepted = 0,
	Rejected = 1,
}

stack_capture_exchange :: proc(capture: ^Stack_Capture, bytes: []u8) -> common.Status
{
	offset: int
	for offset < len(bytes)
	{
		written: int
		error: os.Error
		written, error = os.write(capture.file, bytes[offset:])
		if error != nil || written == 0
		{
			return .Io_Failed
		}
		offset += written
	}
	reply: [4]u8
	offset = 0
	for offset < len(reply)
	{
		received: int
		error: os.Error
		received, error = os.read(capture.file, reply[offset:])
		if error != nil || received == 0
		{
			return .Io_Failed
		}
		offset += received
	}
	value: u32 = u32(reply[0]) | u32(reply[1]) << 8 | u32(reply[2]) << 16 | u32(reply[3]) << 24
	return .Ok if value == u32(Stack_Reply.Accepted) else .Io_Failed
}
