package main

import "core:fmt"
import "core:mem"
import "core:os"
import "polygon:common"
import "polygon:cases"

Ray_Recording :: struct
{
	file: ^os.File,
	path: string,
	partial: string,
	header: [16]u32,
}

ray_recording_begin :: proc(recording: ^Ray_Recording, arguments: ^Arguments, execution: ^common.Execution) -> common.Status
{
	if arguments.recording_mode == .Off
	{
		return .Ok
	}
	recording.path = arguments.recording_output
	recording.partial = fmt.aprintf("%s.partial", recording.path)
	error: os.Error
	recording.file, error = os.open(recording.partial, {.Write, .Create, .Excl})
	if error != nil
	{
		return .Io_Failed
	}
	recording.header[0] = 0x31485452
	recording.header[1] = 1
	recording.header[2] = arguments.threads
	recording.header[3] = arguments.repeat
	recording.header[4] = execution.ray_tracing.width
	recording.header[5] = execution.ray_tracing.height
	bytes: []u8 = mem.slice_to_bytes(recording.header[:])
	copy(bytes[32:64], "entasis")
	written: int
	written, error = os.write(recording.file, bytes)
	return .Ok if error == nil && written == len(bytes) else .Io_Failed
}

ray_recording_append :: proc(recording: ^Ray_Recording, owner: ^cases.Ray_Runtime, measured_suite: u32) -> common.Status
{
	phase: common.Ray_Phase = owner.phase.phase
	if recording.file == nil || measured_suite != owner.phase.view ||
		(phase != .Primary && phase != .Shadow && phase != .Reflection && phase != .Ambient && phase != .Updated)
	{
		return .Ok
	}
	header: [8]u32 = {owner.phase.view, u32(phase), u32(owner.api), u32(len(owner.outputs)), u32(len(owner.hits)),
		u32(len(owner.outputs)), 0, 0}
	segments: [3][]u8 = {mem.slice_to_bytes(header[:]), mem.slice_to_bytes(owner.outputs), mem.slice_to_bytes(owner.hits)}
	for bytes in segments
	{
		written: int
		error: os.Error
		written, error = os.write(recording.file, bytes)
		if error != nil || written != len(bytes)
		{
			return .Io_Failed
		}
	}
	recording.header[6] += 1
	return .Ok
}

ray_recording_complete :: proc(recording: ^Ray_Recording) -> common.Status
{
	if recording.file == nil
	{
		return .Ok
	}
	recording.header[7] = 1
	position: i64
	error: os.Error
	position, error = os.seek(recording.file, 0, .Start)
	if error != nil || position != 0
	{
		return .Io_Failed
	}
	bytes: []u8 = mem.slice_to_bytes(recording.header[:])
	written: int
	written, error = os.write(recording.file, bytes)
	if error != nil || written != len(bytes)
	{
		return .Io_Failed
	}
	if os.close(recording.file) != nil
	{
		recording.file = nil
		return .Io_Failed
	}
	recording.file = nil
	return .Ok if os.rename(recording.partial, recording.path) == nil else .Io_Failed
}

ray_recording_destroy :: proc(recording: ^Ray_Recording)
{
	if recording.file != nil
	{
		os.close(recording.file)
	}
	delete(recording.partial)
	recording^ = {}
}
