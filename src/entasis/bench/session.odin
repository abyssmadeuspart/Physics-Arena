package main

import "core:mem"
import "core:math"
import "core:time"
import "polygon:common"
import "polygon:cases"
import entasis "entasis:entasis"

Session :: struct
{
	pool: entasis.Buffer_Pool,
	runtime: common.Runtime,
	query: cases.Query_Runtime,
	wall: cases.Wall_Observations,
	execution: ^common.Execution,
	recording_mode: common.Recording_Mode,
	verification_mode: Verification_Mode,
	capture: ^Stack_Capture,
	timings: []f64,
	completed: u32,
	elapsed_ms: f64,
	latest_ms: f64,
}

session_build_world :: proc(session: ^Session, threads: u32) -> common.Status
{
	status: common.Status = common.runtime_init(&session.runtime, session.execution, threads, &session.pool)
	if status != .Ok
	{
		return status
	}
	switch session.execution.family
	{
	case .Container:
		return cases.container_build(&session.runtime, session.execution)
	case .Contact_Islands:
		return cases.islands_build(&session.runtime, session.execution)
	case .Pyramid:
		return cases.pyramid_build(&session.runtime, session.execution)
	case .Pyramid_Wall:
		observations: ^cases.Wall_Observations
		if session.verification_mode == .On
		{
			observations = &session.wall
		}
		return cases.pyramid_wall_build(&session.runtime, session.execution, observations)
	case .Query:
		return cases.query_build(&session.query, &session.runtime, session.execution, session.recording_mode)
	case .Ray_Tracing:
		return .Invalid
	}
	unreachable()
}

session_init :: proc(session: ^Session, execution: ^common.Execution, threads: u32, recording_mode: common.Recording_Mode, verification_mode: Verification_Mode) -> common.Status
{
	session.execution = execution
	session.recording_mode = recording_mode
	session.verification_mode = verification_mode
	allocation: mem.Allocator_Error
	session.timings, allocation = make([]f64, int(execution.measured_count))
	if allocation != nil
	{
		return .Capacity
	}
	if execution.family == .Pyramid_Wall && verification_mode == .On
	{
		for index: u32 = 0; index < min(4, execution.measured_count); index += 1
		{
			session.wall.inputs[index], allocation = make([]entasis.Body_State, int(execution.dynamic_count))
			if allocation != nil
			{
				return .Capacity
			}
		}
	}
	if execution.family != .Query && entasis.buffer_pool_init(&session.pool, 65536) != .Ok
	{
		return .Physics_Failed
	}
	status: common.Status = session_build_world(session, threads)
	if status != .Ok || session.capture == nil
	{
		return status
	}
	session.capture.segment += 1
	return stack_capture_append(session.capture, &session.runtime, 0, 0)
}

session_destroy :: proc(session: ^Session) -> common.Status
{
	cases.query_destroy(&session.query)
	status: common.Status = common.runtime_destroy(&session.runtime)
	if status != .Ok
	{
		return status
	}
	if session.pool.state == .Ready && entasis.buffer_pool_destroy(&session.pool) != .Ok
	{
		status = .Physics_Failed
	}
	for inputs in session.wall.inputs
	{
		delete(inputs)
	}
	delete(session.timings)
	session^ = {}
	return status
}

session_step :: proc(session: ^Session) -> common.Status
{
	if session.completed >= session.execution.measured_count
	{
		return .Invalid
	}
	status: common.Status
	milliseconds: f64
	if session.execution.family == .Query
	{
		milliseconds, status = cases.query_batch(&session.query, .Measured, session.completed + 1)
	}
	else
	{
		if session.execution.family == .Pyramid
		{
			status = cases.pyramid_prepare_step(&session.runtime, session.execution, session.completed)
			if status != .Ok
			{
				return status
			}
		}
		start: time.Tick = time.tick_now()
		native_status: entasis.Status = entasis.world_step(&session.runtime.world, 1.0 / f32(session.execution.timestep_hz))
		milliseconds = f64(time.tick_since(start)) / f64(time.Millisecond)
		if native_status != .Ok
		{
			return .Physics_Failed
		}
	}
	if status != .Ok || milliseconds <= 0 || math.is_nan(milliseconds) || math.is_inf(milliseconds)
	{
		return .Physics_Failed
	}
	session.timings[session.completed] = milliseconds
	session.latest_ms = milliseconds
	session.elapsed_ms += milliseconds
	session.completed += 1
	if session.execution.family == .Pyramid_Wall && session.verification_mode == .On
	{
		index: int = cases.pyramid_wall_observation_index(session.completed, session.execution.measured_count)
		if index >= 0
		{
			return cases.pyramid_wall_capture(&session.runtime, session.wall.inputs[index], &session.wall.samples[index])
		}
	}
	return .Ok
}

session_warmup :: proc(session: ^Session) -> common.Status
{
	if session.execution.warmup_count == 0
	{
		return .Ok
	}
	for index: u32 = 0; index < session.execution.warmup_count; index += 1
	{
		if session.execution.family == .Query
		{
			elapsed: f64
			status: common.Status
			elapsed, status = cases.query_batch(&session.query, .Warmup, index + 1)
			if status != .Ok
			{
				return status
			}
		}
		else if entasis.world_step(&session.runtime.world, 1.0 / f32(session.execution.timestep_hz)) != .Ok
		{
			return .Physics_Failed
		}
		if session.capture != nil
		{
			status: common.Status = stack_capture_append(session.capture, &session.runtime, 1, index + 1)
			if status != .Ok
			{
				return status
			}
		}
	}
	if session.execution.family == .Query
	{
		return cases.query_measurement_reset(&session.query)
	}
	if session.execution.family == .Contact_Islands || session.execution.family == .Pyramid_Wall
	{
		// these measurements continue from the warmup bodies and pools
		return .Ok
	}
	threads: u32 = session.runtime.threads
	if common.runtime_destroy(&session.runtime) != .Ok
	{
		return .Physics_Failed
	}
	status: common.Status = session_build_world(session, threads)
	if status != .Ok || session.capture == nil
	{
		return status
	}
	session.capture.segment += 1
	return stack_capture_append(session.capture, &session.runtime, 0, 0)
}

count_invalid_transforms :: proc(session: ^Session) -> u32
{
	invalid: u32
	for index: u32 = 0; index < session.execution.dynamic_count; index += 1
	{
		pose: entasis.Rigid_Pose
		status: common.Status
		pose, status = common.body_pose(&session.runtime, index)
		values: [7]f32 = {pose.position.x, pose.position.y, pose.position.z, pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w}
		if status != .Ok
		{
			invalid += 1
			continue
		}
		for value in values
		{
			if math.is_nan(value) || math.is_inf(value)
			{
				invalid += 1
				break
			}
		}
	}
	return invalid
}
