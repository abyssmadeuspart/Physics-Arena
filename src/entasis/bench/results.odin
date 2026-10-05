package main

import "core:encoding/csv"
import "core:fmt"
import "core:io"
import "core:os"
import "core:strings"
import "polygon:common"
import entasis "entasis:entasis"

raw_header :: "raw_schema_version,repeat_index,fixture_semantic,fixture_revision,physics_settings,body_count,shape_count,query_count,constraint_count,invalid_transform_count,case_status,metric_status,effective_thread_count,effective_worker_count,actual_taskgraph_worker_count,completed_work_unit_count,workload_elapsed_ms,render_elapsed_ms,present_wait_ms,visual_validation_status,proof_path\n"
observation_header :: "repeat_index,metric_id,phase_id,sample_index,value\n"

physics_settings :: proc(session: ^Session, storage: []u8) -> string
{
	if session.execution.family == .Query || session.execution.family == .Ray_Tracing
	{
		return fmt.bprintf(storage,
			"static mixed queries; solver=unused; warmed world/pools retained; complete batch and family dispatch/reduction timed; hits checked each batch; worker_count=%d",
			session.runtime.threads - 1)
	}
	sleep: string = "disabled"
	if session.execution.sleep_mode == 1
	{
		sleep = "enabled"
	}
	ccd: string = "discrete"
	if session.execution.ccd_mode == 1
	{
		ccd = "swept_ccd"
	}
	restitution_threshold: string = ""
	if session.execution.restitution > 0
	{
		restitution_threshold = "; restitution_threshold_mps=1"
	}
	description: entasis.World_Description = common.benchmark_world_description(session.execution, session.runtime.threads)
	return fmt.bprintf(storage,
		"velocity_iterations=%d; substeps=%d; linear_damping=0; angular_damping=0; sleep=%s; ccd=%s; friction=%.6g; restitution=%.6g%s; worker_count=%d",
		description.solve.velocity_iterations, description.solve.substeps, sleep, ccd, session.execution.friction,
		session.execution.restitution, restitution_threshold, session.runtime.threads - 1)
}

query_observation_bits :: proc(session: ^Session, ordinal: int) -> u64
{
	if ordinal >= 3
	{
		return session.query.last_hits[ordinal - 3]
	}
	counts: [3]u32 = {session.execution.query.ray_count, session.execution.query.sphere_cast_count, session.execution.query.overlap_count}
	value: f64
	if session.query.total_ms[ordinal] > 0
	{
		value = f64(counts[ordinal]) * f64(session.completed) * 1000 / session.query.total_ms[ordinal]
	}
	return transmute(u64)value
}

replace_csv :: proc(path: string, header: string, records: [][]string, append_mode: u32) -> common.Status
{
	builder: strings.Builder
	defer strings.builder_destroy(&builder)
	if append_mode != 0
	{
		file: ^os.File
		error: os.Error
		file, error = os.open(path, {.Read, .Write, .Create})
		if error != nil
		{
			return .Io_Failed
		}
		existing: []u8
		existing, error = os.read_entire_file(file, context.allocator)
		close_error: os.Error = os.close(file)
		defer delete(existing)
		if error != nil || close_error != nil
		{
			return .Io_Failed
		}
		if len(existing) != 0
		{
			if !strings.has_prefix(string(existing), header)
			{
				return .Invalid
			}
			strings.write_bytes(&builder, existing)
		}
	}
	if strings.builder_len(builder) == 0
	{
		strings.write_string(&builder, header)
	}
	writer: csv.Writer
	csv.writer_init(&writer, strings.to_writer(&builder))
	// the memory-backed builder has no flush operation, and each record is written directly
	for record in records
	{
		if csv.write(&writer, record) != nil
		{
			return .Io_Failed
		}
	}
	temporary: string = fmt.aprintf("%s.tmp", path)
	defer delete(temporary)
	if os.write_entire_file(temporary, strings.to_string(builder)) != nil
	{
		return .Io_Failed
	}
	if os.rename(temporary, path) != nil
	{
		return .Io_Failed
	}
	return .Ok
}

write_results :: proc(session: ^Session, arguments: ^Arguments) -> common.Status
{
	if session.completed != session.execution.measured_count || !strings.has_suffix(arguments.output, "_raw.csv")
	{
		return .Invalid
	}
	observation_path: string = fmt.aprintf("%s_observations.csv", strings.trim_suffix(arguments.output, "_raw.csv"))
	defer delete(observation_path)
	observation_ids: [6]string = {"ray_queries_per_second", "sphere_cast_queries_per_second", "overlap_queries_per_second",
		"ray_hit_count", "sphere_cast_hit_count", "overlap_hit_count"}
	observation_storage: [6][2][64]u8
	observation_rows: [6][5]string
	observation_records: [6][]string
	observation_count: int
	if session.execution.family == .Query
	{
		observation_count = 6
		for index in 0..<6
		{
			observation_rows[index] = {fmt.bprintf(observation_storage[index][0][:], "%d", arguments.repeat), observation_ids[index], "final", "0", ""}
			bits: u64 = query_observation_bits(session, index)
			if index < 3
			{
				observation_rows[index][4] = fmt.bprintf(observation_storage[index][1][:], "%.17g", transmute(f64)bits)
			}
			else
			{
				observation_rows[index][4] = fmt.bprintf(observation_storage[index][1][:], "%d", bits)
			}
			observation_records[index] = observation_rows[index][:]
		}
	}
	status: common.Status
	if session.execution.family == .Pyramid_Wall
	{
		status = write_wall_observations(session, arguments, observation_path)
	}
	else
	{
		status = replace_csv(observation_path, observation_header, observation_records[:observation_count], 1)
	}
	if status != .Ok
	{
		return status
	}
	storage: [21][64]u8
	settings_storage: [256]u8
	invalid_count: u32 = count_invalid_transforms(session)
	if session.execution.family == .Pyramid_Wall && session.verification_mode == .On
	{
		for sample in session.wall.samples
		{
			invalid_count += u32(sample.invalid)
		}
	}
	case_status: string = "ok"
	if invalid_count != 0
	{
		case_status = "invalid_result"
	}
	row: [21]string = {"3", fmt.bprintf(storage[1][:], "%d", arguments.repeat), session.execution.semantic,
		fmt.bprintf(storage[3][:], "%d", session.execution.revision), physics_settings(session, settings_storage[:]),
		fmt.bprintf(storage[5][:], "%d", session.execution.body_count), fmt.bprintf(storage[6][:], "%d", session.execution.shape_count),
		fmt.bprintf(storage[7][:], "%d", session.execution.query_count), "0", fmt.bprintf(storage[9][:], "%d", invalid_count), case_status, case_status,
		fmt.bprintf(storage[12][:], "%d", arguments.threads), fmt.bprintf(storage[13][:], "%d", arguments.threads - 1), "",
		fmt.bprintf(storage[15][:], "%d", session.completed), fmt.bprintf(storage[16][:], "%.9f", session.elapsed_ms), "", "", "", ""}
	records: [1][]string = {row[:]}
	status = replace_csv(arguments.output, raw_header, records[:], 1)
	if status != .Ok
	{
		return status
	}
	if len(arguments.timing_output) > 0
	{
		status = write_timings(session, arguments.timing_output)
		if status != .Ok
		{
			return status
		}
	}
	if invalid_count != 0
	{
		return .Physics_Failed
	}
	return .Ok
}

write_timings :: proc(session: ^Session, path: string) -> common.Status
{
	temporary: string = fmt.aprintf("%s.tmp", path)
	defer delete(temporary)
	file: ^os.File
	error: os.Error
	file, error = os.open(temporary, {.Write, .Create, .Trunc})
	if error != nil
	{
		return .Io_Failed
	}
	writer: csv.Writer
	csv.writer_init(&writer, os.to_writer(file))
	header: [3]string = {"step_index", "physics_step_ms", "render_frame_ms"}
	write_error: io.Error = csv.write(&writer, header[:])
	for milliseconds, index in session.timings[:session.completed]
	{
		if write_error != nil
		{
			break
		}
		storage: [2][64]u8
		row: [3]string = {fmt.bprintf(storage[0][:], "%d", index + 1), fmt.bprintf(storage[1][:], "%.9f", milliseconds), ""}
		write_error = csv.write(&writer, row[:])
	}
	error = os.close(file)
	if error != nil || write_error != nil
	{
		return .Io_Failed
	}
	if os.rename(temporary, path) != nil
	{
		return .Io_Failed
	}
	return .Ok
}
