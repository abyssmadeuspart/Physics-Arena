package main

import "core:fmt"
import "polygon:common"
import "polygon:cases"

write_wall_observations :: proc(session: ^Session, arguments: ^Arguments, path: string) -> common.Status
{
	if session.verification_mode == .Off
	{
		return replace_csv(path, observation_header, nil, 1)
	}
	ids: [9]string = {"centre_of_mass_height", "lateral_rms", "translational_energy", "rotational_energy",
		"potential_energy", "floor_penetration", "escaped_body_count", "invalid_body_count", "observation_elapsed_ms"}
	sample_count: int = int(min(4, session.execution.measured_count))
	storage: [37][3][64]u8
	rows: [37][5]string
	records: [37][]string
	for sample, ordinal in session.wall.samples[:sample_count]
	{
		values: [9]f64 = {sample.height, sample.lateral_rms, sample.translational_energy, sample.rotational_energy,
			sample.potential_energy, sample.penetration, f64(sample.escaped), f64(sample.invalid), sample.elapsed_ms}
		for value, field in values
		{
			index: int = field * sample_count + ordinal
			if field == 8
			{
				index = 8 * sample_count + 1 + ordinal
			}
			rows[index] = {fmt.bprintf(storage[index][0][:], "%d", arguments.repeat), ids[field], "observation",
				fmt.bprintf(storage[index][1][:], "%d", cases.pyramid_wall_observation_step(session.execution.measured_count, u32(ordinal))), fmt.bprintf(storage[index][2][:], "%.17g", value)}
			if field == 6 || field == 7
			{
				count: u64 = sample.escaped
				if field == 7
				{
					count = sample.invalid
				}
				rows[index][4] = fmt.bprintf(storage[index][2][:], "%d", count)
			}
			records[index] = rows[index][:]
		}
	}
	rows[8 * sample_count] = {fmt.bprintf(storage[8 * sample_count][0][:], "%d", arguments.repeat), "initial_potential_energy", "construction", "0",
		fmt.bprintf(storage[8 * sample_count][2][:], "%.17g", session.wall.initial_potential_energy)}
	records[8 * sample_count] = rows[8 * sample_count][:]
	return replace_csv(path, observation_header, records[:9 * sample_count + 1], 1)
}
