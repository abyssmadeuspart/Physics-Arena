package main

import "core:fmt"
import "core:os"
import "polygon:common"
import "polygon:cases"

ray_run_probes :: proc(arguments: ^Arguments, execution: ^common.Execution) -> common.Status
{
	scene_path: string = fmt.aprintf("%s/probe-scene.rtc", arguments.ray_corpus)
	defer delete(scene_path)
	scene: common.Ray_Scene
	defer common.ray_scene_destroy(&scene)
	if common.ray_scene_read(scene_path, &scene) != .Ok || len(scene.colliders) == 0 || len(scene.colliders) > 128
	{
		return .Invalid
	}
	probe_execution: common.Execution = execution^
	probe_execution.kinematic_count = 0
	probe_execution.static_count = u32(len(scene.colliders))
	probe_execution.ray_tracing.primitive_count = 128
	owner: cases.Ray_Runtime
	defer cases.ray_destroy(&owner)
	status: common.Status = cases.ray_build(&owner, &scene, &probe_execution, arguments.threads)
	if status != .Ok
	{
		return status
	}
	status = ray_admit_corpus(&owner, arguments, execution, 1)
	if status != .Ok
	{
		return status
	}
	path: string = fmt.aprintf("entasis_t%d_r%d_ray-capabilities.csv", arguments.threads, arguments.repeat)
	defer delete(path)
	file: ^os.File
	error: os.Error
	file, error = os.open(path, {.Write, .Create, .Trunc})
	if error != nil
	{
		return .Io_Failed
	}
	defer os.close(file)
	header: string = "engine_id,thread_count,repeat_index,view,capability,api,status,queries,hit_rays,reference_mismatches,native_errors,overflow_reports,world_colliders\n"
	written: int
	written, error = os.write(file, transmute([]u8)header)
	if error != nil || written != len(header)
	{
		return .Io_Failed
	}
	names: [8]string = {"inside-origin", "mesh-backfaces", "surface-start", "range-endpoints", "grazing", "far-origin", "enumerator-overflow", "concurrent-read"}
	for view: u32 = 0; view < 6; view += 1
	{
		for probe: u32 = 0; probe < 8; probe += 1
		{
			phase_path: string = fmt.aprintf("%s/probe-view-%d-%s.rtr", arguments.ray_corpus, view, names[probe])
			phase: ^common.Ray_Corpus_Phase = &owner.input
			status = common.ray_phase_read(phase_path, phase)
			delete(phase_path)
			if status != .Ok || len(phase.rays) != 512 || phase.view != view
			{
				return .Invalid
			}
			status = cases.ray_prepare_phase(&owner, phase, .Ordinary)
			if status != .Ok
			{
				return status
			}
			cases.ray_reset_outputs(&owner)
			native_errors, overflow_reports: u64
			if probe == 7
			{
				_, status = cases.ray_execute(&owner)
				native_errors = u64(status != .Ok)
			}
			else
			{
				for index: u32 = 0; index < 512; index += 1
				{
					status = cases.ray_query_one(&owner, &owner.lanes[0], index)
					if status == .Capacity && probe == 6
					{
						overflow_reports += 1
					}
					else if status != .Ok
					{
						native_errors += 1
					}
				}
			}
			validation: common.Ray_Validation = common.ray_validate_outputs(phase, owner.outputs, owner.hits)
			outcome: string = "supported"
			if native_errors != 0 || (probe == 6 && overflow_reports != 512) || (probe == 7 && validation.errors != 0)
			{
				outcome = "failed"
			}
			else if probe != 6 && validation.errors != 0
			{
				outcome = "native_semantics_differ"
			}
			storage: [1024]u8
			line: string = fmt.bprintf(storage[:], "entasis,%d,%d,%d,%s,ordinary,%s,512,%d,%d,%d,%d,%d\n",
				arguments.threads, arguments.repeat, view, names[probe], outcome, validation.hit_rays,
				validation.errors if probe != 6 else 0, native_errors, overflow_reports, len(scene.colliders))
			written, error = os.write(file, transmute([]u8)line)
			if error != nil || written != len(line)
			{
				return .Io_Failed
			}
		}
	}
	return .Ok
}
