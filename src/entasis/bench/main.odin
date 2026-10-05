package main

import "core:fmt"
import "core:os"
import "polygon:common"
import "polygon:cases"

run_recorded :: proc(arguments: ^Arguments, execution: ^common.Execution) -> (result: common.Status)
{
	session: Session
	defer
	{
		cleanup: common.Status = session_destroy(&session)
		if result == .Ok && cleanup != .Ok
		{
			result = cleanup
		}
	}
	status: common.Status = session_init(&session, execution, arguments.threads, arguments.recording_mode, arguments.verification_mode)
	if status != .Ok
	{
		return status
	}
	capture: Stack_Capture
	defer stack_capture_destroy(&capture)
	status = stack_capture_open(&capture, arguments, execution)
	if status != .Ok
	{
		return status
	}
	if capture.file != nil
	{
		session.capture = &capture
		status = stack_capture_append(&capture, &session.runtime, 0, 0)
		if status != .Ok
		{
			return status
		}
	}
	status = session_warmup(&session)
	if status != .Ok
	{
		return status
	}
	recording: common.Recording_Writer
	defer
	{
		if arguments.recording_mode == .On
		{
			cleanup: common.Status = common.recording_destroy(&recording)
			if result == .Ok && cleanup != .Ok
			{
				result = cleanup
			}
		}
	}
	frame_size: int
	if arguments.recording_mode == .On
	{
		scene: []u8
		layout: common.Recording_Layout
		scene, layout, status = recording_scene(&session, arguments)
		if status != .Ok
		{
			return status
		}
		status = common.recording_begin(&recording, arguments.recording_output, scene, layout)
		delete(scene)
		if status != .Ok
		{
			return status
		}
		frame_size, status = recording_frame(&session, recording.frame)
		if status != .Ok
		{
			return status
		}
		status = common.recording_append(&recording, 0, frame_size)
		if status != .Ok
		{
			return status
		}
	}
	for session.completed < execution.measured_count
	{
		status = session_step(&session)
		if status != .Ok
		{
			return status
		}
		if capture.file != nil
		{
			status = stack_capture_append(&capture, &session.runtime, 2, session.completed)
			if status != .Ok
			{
				return status
			}
		}
		if arguments.recording_mode == .Off
		{
			continue
		}
		frame_size, status = recording_frame(&session, recording.frame)
		if status != .Ok
		{
			return status
		}
		status = common.recording_append(&recording, u64(session.completed), frame_size)
		if status != .Ok
		{
			return status
		}
	}
	if arguments.recording_mode == .On
	{
		status = common.recording_complete(&recording)
		if status != .Ok
		{
			return status
		}
	}
	status = stack_capture_close(&capture)
	if status != .Ok
	{
		return status
	}
	if execution.family == .Pyramid_Wall && arguments.verification_mode == .On
	{
		for index: u32 = 0; index < min(4, execution.measured_count); index += 1
		{
			status = cases.pyramid_wall_reduce(session.wall.inputs[index], execution, &session.wall.samples[index])
			if status != .Ok
			{
				return status
			}
		}
	}
	return write_results(&session, arguments)
}

run :: proc() -> common.Status
{
	arguments: Arguments
	status: common.Status
	arguments, status = parse_arguments(os.args[1:])
	if status != .Ok
	{
		return status
	}
	storage: [2048]u8
	execution: common.Execution
	execution, status = common.decode_execution_hex(arguments.contract, storage[:])
	if status != .Ok
	{
		return status
	}
	status = validate_stack_endpoint(&arguments, &execution)
	if status != .Ok
	{
		return status
	}
	if execution.family == .Ray_Tracing
	{
		return run_ray_tracing(&arguments, &execution)
	}
	if arguments.ray_stage != .Heavy
	{
		return .Invalid
	}
	return run_recorded(&arguments, &execution)
}

main :: proc()
{
	status: common.Status = run()
	if status == .Cancelled
	{
		fmt.eprintln("run_cancelled engine=entasis")
		return
	}
	if status != .Ok
	{
		fmt.eprintf("run_failed engine=entasis status=%v\n", status)
		os.exit(2)
	}
}
