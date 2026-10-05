package main

import "core:strconv"
import "core:strings"
import "polygon:common"

Ray_Run_Stage :: enum
{
	Heavy,
	Preflight,
}

Verification_Mode :: enum
{
	On,
	Off,
}

Arguments :: struct
{
	contract: string,
	stack_stream: string,
	ray_corpus: string,
	ray_stage: Ray_Run_Stage,
	threads: u32,
	repeat: u32,
	output: string,
	timing_output: string,
	recording_output: string,
	recording_mode: common.Recording_Mode,
	verification_mode: Verification_Mode,
}

parse_arguments :: proc(values: []string) -> (Arguments, common.Status)
{
	result: Arguments = {threads = 1, output = "polygon_results.csv"}
	seen: u32
	for argument in values
	{
		separator: int = strings.index_byte(argument, '=')
		if separator <= 0 || separator == len(argument) - 1
		{
			return {}, .Invalid
		}
		key: string = argument[:separator]
		value: string = argument[separator + 1:]
		index: u32
		switch key
		{
		case "--case-contract":
			index = 0
			result.contract = value
		case "--thread-count":
			index = 1
		case "--repeat-index":
			index = 2
		case "--output":
			index = 3
			result.output = value
		case "--step-timing-output":
			index = 4
			result.timing_output = value
		case "--recording-output":
			index = 5
			result.recording_output = value
			result.recording_mode = .On
		case "--stack-stream":
			index = 9
			if !strings.has_prefix(value, "\\\\.\\pipe\\") || len(value) <= 9
			{
				return {}, .Invalid
			}
			result.stack_stream = value
		case "--verify":
			index = 8
			switch value
			{
			case "on":
				result.verification_mode = .On
			case "off":
				result.verification_mode = .Off
			case:
				return {}, .Invalid
			}
		case "--ray-corpus":
			index = 6
			result.ray_corpus = value
		case "--ray-stage":
			index = 7
			if value != "preflight"
			{
				return {}, .Invalid
			}
			result.ray_stage = .Preflight
		case:
			return {}, .Invalid
		}
		mask: u32 = 1 << index
		if (seen & mask) != 0
		{
			return {}, .Invalid
		}
		seen |= mask
		if index == 1 || index == 2
		{
			number: u64
			ok: bool
			number, ok = strconv.parse_u64(value, 10)
			if !ok || number > 0xffffffff
			{
				return {}, .Invalid
			}
			switch index
			{
			case 1:
				result.threads = u32(number)
			case 2:
				result.repeat = u32(number)

			}
		}
	}
	if (seen & 1) == 0 || result.threads == 0 || result.threads > 256
	{
		return {}, .Invalid
	}
	return result, .Ok
}

validate_stack_endpoint :: proc(arguments: ^Arguments, execution: ^common.Execution) -> common.Status
{
	required: int = 0
	if arguments.verification_mode == .On && (execution.family == .Container || execution.family == .Contact_Islands || execution.family == .Pyramid || execution.family == .Pyramid_Wall)
	{
		required = 1
	}
	if (required != 0 && len(arguments.stack_stream) == 0) || (required == 0 && len(arguments.stack_stream) != 0)
	{
		return .Invalid
	}
	return .Ok
}
