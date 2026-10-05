package main

import "core:fmt"
import "core:os"
import "core:strings"
import "polygon:common"
import "polygon:cases"

ray_result_header :: "engine_id,thread_count,repeat_index,view,suite,phase,api,status,queries,hit_rays,query_ms,update_ms,conditioning_ms,validation_ms,errors,written,setup_ms,suite_ms,buffer_bytes\n"

ray_write_phase :: proc(file: ^os.File, arguments: ^Arguments, view, suite: u32, phase: common.Ray_Phase, api: common.Ray_Api,
	status: string, queries: u64, validation: common.Ray_Validation, query_ms, update_ms, conditioning_ms, validation_ms, setup_ms, suite_ms: f64,
	buffer_bytes: u64) -> common.Status
{
	api_name: string = "ordinary" if api == .Ordinary else "native-batch"
	storage: [1024]u8
	line: string = fmt.bprintf(storage[:], "entasis,%d,%d,%d,%d,%s,%s,%s,%d,%d,%.9f,%.9f,%.9f,%.9f,%d,%d,%.9f,%.9f,%d\n",
		arguments.threads, arguments.repeat, view, suite, common.ray_phase_name(phase), api_name, status, queries, validation.hit_rays,
		query_ms, update_ms, conditioning_ms, validation_ms, validation.errors, validation.written, setup_ms, suite_ms, buffer_bytes)
	if status == "unsupported"
	{
		line = fmt.bprintf(storage[:], "entasis,%d,%d,%d,%d,%s,%s,unsupported,,,,,,,,,,,\n",
			arguments.threads, arguments.repeat, view, suite, common.ray_phase_name(phase), api_name)
	}
	if status == "execution_failed" && query_ms == 0
	{
		line = fmt.bprintf(storage[:], "entasis,%d,%d,%d,%d,%s,%s,execution_failed,%d,,,,,,,,,,\n",
			arguments.threads, arguments.repeat, view, suite, common.ray_phase_name(phase), api_name, queries)
	}

	written: int
	error: os.Error
	written, error = os.write(file, transmute([]u8)line)
	return .Ok if error == nil && written == len(line) else .Io_Failed
}

ray_write_failure :: proc(file: ^os.File, owner: ^cases.Ray_Runtime, validation: common.Ray_Validation) -> common.Status
{
	for failure: u32 = 0; failure < validation.failing_count; failure += 1
	{
		index: u32 = validation.failing_rays[failure]
		input: common.Ray_Input = owner.phase.rays[index]
		output: common.Ray_Output = owner.outputs[index]
		range: common.Ray_Range = owner.phase.expected[index]
		storage: [1024]u8
		api_name: string = "ordinary" if owner.api == .Ordinary else "native-batch"
		line: string = fmt.bprintf(storage[:], "view=%d phase=%s api=%s ray=%d pixel=%d origin=%.9g,%.9g,%.9g translation=%.9g,%.9g,%.9g length=%.9g mask=%d written=%d status=%v expected_first=%d expected_count=%d actual_first=%d actual_count=%d\n",
			owner.phase.view, common.ray_phase_name(owner.phase.phase), api_name, index, input.pixel,
			input.origin.x, input.origin.y, input.origin.z, input.translation.x, input.translation.y, input.translation.z,
			input.length, input.mask, output.written, output.status, range.first, range.count, output.first, output.count)
		written: int
		error: os.Error
		written, error = os.write(file, transmute([]u8)line)
		if error != nil || written != len(line)
		{
			return .Io_Failed
		}
		if u64(output.first) + u64(output.count) > u64(len(owner.hits))
		{
			line = "actual_range=invalid\n"
			written, error = os.write(file, transmute([]u8)line)
			if error != nil || written != len(line)
			{
				return .Io_Failed
			}
			continue
		}
		for event: u32 = 0; event < output.count; event += 1
		{
			actual: common.Ray_Hit = owner.hits[output.first + event]
			line = fmt.bprintf(storage[:], "actual_event=%d collider=%d source=%d distance=%.17g normal=%.9g,%.9g,%.9g flags=%d\n",
				event, actual.collider, actual.source, actual.distance, actual.normal.x, actual.normal.y, actual.normal.z, actual.flags)
			written, error = os.write(file, transmute([]u8)line)
			if error != nil || written != len(line)
			{
				return .Io_Failed
			}
		}
	}
	return .Ok
}

ray_write_summary :: proc(arguments: ^Arguments, execution: ^common.Execution, durations: []f64, correctness_errors: u64) -> common.Status
{
	observation_path: string = fmt.aprintf("%s_observations.csv", strings.trim_suffix(arguments.output, "_raw.csv"))
	defer delete(observation_path)
	storage: [64]u8
	error_storage: [64]u8
	observation: [5]string = {fmt.bprintf(storage[:], "%d", arguments.repeat), "ray_correctness_errors", "final", "0", fmt.bprintf(error_storage[:], "%d", correctness_errors)}
	observations: [1][]string = {observation[:]}
	status: common.Status = replace_csv(observation_path, observation_header, observations[:], 1)
	if status != .Ok
	{
		return status
	}
	session: Session = {execution = execution, completed = u32(len(durations))}
	for duration in durations
	{
		session.elapsed_ms += duration
	}
	settings_storage: [512]u8
	settings: string = fmt.bprintf(settings_storage[:], "ordinary coherent closest only in headline; public context queries; public scalar native batch separately; no physics step; worker_count=%d", arguments.threads - 1)
	row_storage: [8][64]u8
	row: [21]string = {"3", fmt.bprintf(row_storage[0][:], "%d", arguments.repeat), "ray_tracing", "1",
		settings,
		fmt.bprintf(row_storage[4][:], "%d", execution.body_count), fmt.bprintf(row_storage[5][:], "%d", execution.shape_count), fmt.bprintf(row_storage[6][:], "%d", execution.query_count), "0", "0", "failed" if correctness_errors != 0 else "ok", "ok", fmt.bprintf(row_storage[1][:], "%d", arguments.threads),
		fmt.bprintf(row_storage[2][:], "%d", arguments.threads - 1), "", fmt.bprintf(row_storage[7][:], "%d", execution.measured_count), fmt.bprintf(row_storage[3][:], "%.9f", session.elapsed_ms), "", "", "", ""}
	rows: [1][]string = {row[:]}
	status = replace_csv(arguments.output, raw_header, rows[:], 1)
	if status != .Ok
	{
		return status
	}
	if len(arguments.timing_output) != 0
	{
		// the existing timing writer consumes milliseconds through its duration buffer
		session.timings = durations
		return write_timings(&session, arguments.timing_output)
	}
	return .Ok
}
