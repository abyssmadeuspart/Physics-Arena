#include "physics_arena/bench_types.h"

namespace physics_arena
{
const char* ArenaStatusText(ArenaStatus status)
{
	switch (status)
	{
	case ArenaStatus_Ok:
		return "ok";
	case ArenaStatus_InvalidArgument:
		return "invalid_argument";
	case ArenaStatus_InvalidResult:
		return "invalid_result";
	case ArenaStatus_ToolMissing:
		return "tool_missing";
	case ArenaStatus_ReferenceMismatch:
		return "ref_mismatch";
	case ArenaStatus_BuildFailed:
		return "build_failed";
	case ArenaStatus_RunFailed:
		return "run_failed";
	case ArenaStatus_Interrupted:
		return "interrupted";
	}
	return "invalid_status";
}
}
