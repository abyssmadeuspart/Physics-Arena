package main

import "core:fmt"
import "core:mem"
import "core:os"
import "core:time"
import "polygon:common"
import "polygon:cases"
import entasis "entasis:entasis"

ray_admit_corpus :: proc(owner: ^cases.Ray_Runtime, arguments: ^Arguments, execution: ^common.Execution, probes: u32) -> common.Status
{
	capacity: common.Ray_Phase_Capacity
	probe_names: [8]string = {"inside-origin", "mesh-backfaces", "surface-start", "range-endpoints", "grazing", "far-origin", "enumerator-overflow", "concurrent-read"}
	views: u32 = 6 if probes != 0 else execution.ray_tracing.view_count
	for view: u32 = 0; view < views; view += 1
	{
		count: u32 = 8 if probes != 0 else 9
		for index: u32 = 0; index < count; index += 1
		{
			storage: [4096]u8
			path: string
			if probes != 0
			{
				path = fmt.bprintf(storage[:], "%s/probe-view-%d-%s.rtr", arguments.ray_corpus, view, probe_names[index])
			}
			else
			{
				path = fmt.bprintf(storage[:], "%s/view-%d-%s.rtr", arguments.ray_corpus, view, common.ray_phase_name(common.Ray_Phase(index)))
			}
			metadata: common.Ray_Phase_Metadata
			status: common.Status
			metadata, status = common.ray_phase_metadata(path)
			if status != .Ok || metadata.view != view || (probes == 0 && (u32(metadata.phase) != index || metadata.width != execution.ray_tracing.width || metadata.height != execution.ray_tracing.height)) ||
				(probes != 0 && metadata.rays != 512)
			{
				return .Invalid
			}
			common.ray_include_capacity(&capacity, metadata)
		}
	}
	if probes != 0
	{
		capacity.batch_rays = 0
	}
	return cases.ray_reserve(owner, capacity)
}

Ray_Phase_Measurement :: struct
{
	query_ms, update_ms, conditioning_ms, validation_ms: f64,
	validation: common.Ray_Validation,
}

ray_measure_phase :: proc(owner: ^cases.Ray_Runtime, measured: u32) -> (Ray_Phase_Measurement, common.Status)
{
	result: Ray_Phase_Measurement
	status: common.Status
	if owner.api == .Native_Batch && entasis.query_batch_read_only_status(&owner.world, owner.queries) != .Ok
	{
		return result, .Physics_Failed
	}
	cases.ray_reset_outputs(owner)
	if measured != 0
	{
		start: time.Tick = time.tick_now()
		if owner.phase.phase == .Updated
		{
			_, status = cases.ray_publish_poses(owner, int(owner.phase.view))
			if status != .Ok
			{
				return result, status
			}
		}
		_, status = cases.ray_execute(owner)
		if status != .Ok
		{
			return result, status
		}
		if owner.phase.phase == .Updated
		{
			_, status = cases.ray_publish_poses(owner, -1)
			if status != .Ok
			{
				return result, status
			}
		}
		cases.ray_reset_outputs(owner)
		result.conditioning_ms = time.duration_milliseconds(time.tick_since(start))
	}
	if owner.phase.phase == .Updated
	{
		result.update_ms, status = cases.ray_publish_poses(owner, int(owner.phase.view))
		if status != .Ok
		{
			return result, status
		}
	}
	result.query_ms, status = cases.ray_execute(owner)
	validation_start: time.Tick = time.tick_now()
	result.validation = common.ray_validate_outputs(owner.phase, owner.outputs, owner.hits)
	result.validation_ms = time.duration_milliseconds(time.tick_since(validation_start))
	for output in owner.outputs
	{
		if output.status != .Ok || output.written != 1 || u64(output.first) + u64(output.count) > u64(len(owner.hits))
		{
			status = .Physics_Failed
		}
	}
	return result, status
}

Ray_Result_Pending :: struct
{
	phase: common.Ray_Phase,
	api: common.Ray_Api,
	status: string,
	queries, buffer_bytes: u64,
	measurement: Ray_Phase_Measurement,
}

ray_flush_suite :: proc(rows: []Ray_Result_Pending, file: ^os.File, arguments: ^Arguments, view, suite: u32, setup_ms, suite_ms: f64) -> common.Status
{
	for row in rows
	{
		m: Ray_Phase_Measurement = row.measurement
		status: common.Status = ray_write_phase(file, arguments, view, suite, row.phase, row.api, row.status, row.queries,
			m.validation, m.query_ms, m.update_ms, m.conditioning_ms, m.validation_ms, setup_ms, suite_ms, row.buffer_bytes)
		if status != .Ok
		{
			return status
		}
	}
	return .Ok
}

ray_run_suite :: proc(owner: ^cases.Ray_Runtime, arguments: ^Arguments, execution: ^common.Execution, suite: u32, results, failures: ^os.File,
	recording: ^Ray_Recording, primary_ms: ^f64, correctness_errors: ^u64) -> common.Status
{
	warmup: u32 = 0 if arguments.ray_stage == .Preflight else execution.warmup_count
	measured: u32 = u32(suite >= warmup)
	measured_suite: u32 = suite - warmup if suite >= warmup else suite
	view: u32 = measured_suite % execution.ray_tracing.view_count
	reset_status: common.Status
	_, reset_status = cases.ray_publish_poses(owner, -1)
	if reset_status != .Ok
	{
		return reset_status
	}
	suite_start: time.Tick = time.tick_now()
	rows: [18]Ray_Result_Pending
	row_count: int
	for slot: u32 = 0; slot < 9; slot += 1
	{
		phase_id: common.Ray_Phase = .Updated if slot == 8 else common.Ray_Phase((slot + suite) % 8)
		path: string = fmt.aprintf("%s/view-%d-%s.rtr", arguments.ray_corpus, view, common.ray_phase_name(phase_id))
		phase: ^common.Ray_Corpus_Phase = &owner.input
		status: common.Status = common.ray_phase_read(path, phase)
		delete(path)
		if status != .Ok || phase.view != view || phase.phase != phase_id
		{
			return .Invalid
		}
		for order: u32 = 0; order < 2; order += 1
		{
			api: common.Ray_Api = common.Ray_Api((order + arguments.repeat) % 2)
			if api == .Native_Batch && phase_id != .Primary && phase_id != .Shuffled && phase_id != .Reflection && phase_id != .Updated
			{
				if measured != 0
				{
					rows[row_count] = {phase = phase_id, api = api, status = "unsupported"}
					row_count += 1
				}
				continue
			}
			if phase_id == .Updated
			{
				_, status = cases.ray_publish_poses(owner, -1)
				if status != .Ok
				{
					return status
				}
			}
			status = cases.ray_prepare_phase(owner, phase, api)
			if status != .Ok
			{
				return status
			}
			measurement: Ray_Phase_Measurement
			measurement, status = ray_measure_phase(owner, measured)
			if status != .Ok || measurement.validation.errors != 0
			{
				failure_status: common.Status = ray_write_failure(failures, owner, measurement.validation)
				if failure_status != .Ok
				{
					return failure_status
				}
			}
			if measured != 0
			{
				row_status: string = "failed" if measurement.validation.errors != 0 else "supported"
				if status != .Ok
				{
					row_status = "execution_failed"
				}
				rows[row_count] = {phase_id, api, row_status, u64(len(phase.rays)), owner.buffer_bytes, measurement}
				row_count += 1
				correctness_errors^ += measurement.validation.errors
			}
			if status != .Ok
			{
				flush_status: common.Status = ray_flush_suite(rows[:row_count], results, arguments, view, measured_suite, owner.setup_ms, time.duration_milliseconds(time.tick_since(suite_start)))
				if flush_status != .Ok
				{
					return flush_status
				}
				return status
			}
			if phase_id == .Primary && api == .Ordinary
			{
				primary_ms^ = measurement.query_ms
			}
			if measured != 0
			{
				status = ray_recording_append(recording, owner, measured_suite)
				if status != .Ok
				{
					flush_status: common.Status = ray_flush_suite(rows[:row_count], results, arguments, view, measured_suite, owner.setup_ms, time.duration_milliseconds(time.tick_since(suite_start)))
					return status if flush_status == .Ok else flush_status
				}
			}
		}
	}
	return ray_flush_suite(rows[:row_count], results, arguments, view, measured_suite, owner.setup_ms, time.duration_milliseconds(time.tick_since(suite_start)))
}

run_ray_tracing :: proc(arguments: ^Arguments, execution: ^common.Execution) -> common.Status
{
	if len(arguments.ray_corpus) == 0 || (arguments.ray_stage == .Preflight && arguments.recording_mode != .Off)
	{
		return .Invalid
	}
	scene_path: string = fmt.aprintf("%s/scene.rtc", arguments.ray_corpus)
	defer delete(scene_path)
	scene: common.Ray_Scene
	defer common.ray_scene_destroy(&scene)
	status: common.Status = common.ray_scene_read(scene_path, &scene)
	if status != .Ok || len(scene.colliders) != int(execution.body_count) || len(scene.triangles) != int(execution.triangle_count)
	{
		return .Invalid
	}
	status = ray_run_probes(arguments, execution)
	if status != .Ok
	{
		return status
	}
	owner: cases.Ray_Runtime
	defer cases.ray_destroy(&owner)
	status = cases.ray_build(&owner, &scene, execution, arguments.threads)
	if status != .Ok
	{
		return status
	}
	status = ray_admit_corpus(&owner, arguments, execution, 0)
	if status != .Ok
	{
		return status
	}
	result_path: string = fmt.aprintf("entasis_t%d_r%d_ray-tracing.csv", arguments.threads, arguments.repeat)
	defer delete(result_path)
	failure_path: string = fmt.aprintf("entasis_t%d_r%d_ray-failures.txt", arguments.threads, arguments.repeat)
	defer delete(failure_path)
	results, failures: ^os.File
	error: os.Error
	results, error = os.open(result_path, {.Write, .Create, .Trunc})
	if error != nil
	{
		return .Io_Failed
	}
	defer os.close(results)
	failures, error = os.open(failure_path, {.Write, .Create, .Trunc})
	if error != nil
	{
		return .Io_Failed
	}
	defer os.close(failures)
	written: int
	written, error = os.write(results, transmute([]u8)string(ray_result_header))
	if error != nil || written != len(ray_result_header)
	{
		return .Io_Failed
	}
	recording: Ray_Recording
	defer ray_recording_destroy(&recording)
	status = ray_recording_begin(&recording, arguments, execution)
	if status != .Ok
	{
		return status
	}
	durations: []f64
	allocation: mem.Allocator_Error
	durations, allocation = make([]f64, execution.measured_count)
	if allocation != nil
	{
		return .Physics_Failed
	}
	defer delete(durations)
	correctness_errors: u64
	suite_count: u32 = execution.ray_tracing.view_count if arguments.ray_stage == .Preflight else execution.warmup_count + execution.measured_count
	for suite: u32 = 0; suite < suite_count; suite += 1
	{
		primary_ms: f64
		status = ray_run_suite(&owner, arguments, execution, suite, results, failures, &recording, &primary_ms, &correctness_errors)
		if status != .Ok
		{
			return status
		}
		if arguments.ray_stage != .Preflight && suite >= execution.warmup_count
		{
			durations[suite - execution.warmup_count] = primary_ms
		}
	}
	if arguments.ray_stage != .Preflight
	{
		status = ray_write_summary(arguments, execution, durations[:], correctness_errors)
		if status != .Ok
		{
			return status
		}
	}
	return ray_recording_complete(&recording)
}
