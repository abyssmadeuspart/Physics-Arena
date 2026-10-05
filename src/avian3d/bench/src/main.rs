#[path = "../../../common/stack_state_capture.rs"]
pub mod stack_state_capture;
#[path = "../../cases/avian_pyramid_wall_case.rs"]
pub mod avian_pyramid_wall_case;
#[path = "../../../common/pyramid_wall.rs"]
pub mod pyramid_wall;
#[path = "../../cases/avian_box_container_pile_10k_case.rs"]
pub mod avian_box_container_pile_10k_case;
#[path = "../../cases/avian_box_contact_islands_10k_case.rs"]
pub mod avian_box_contact_islands_10k_case;
#[path = "../../cases/avian_spatial_query_trace_case.rs"]
pub mod avian_spatial_query_trace_case;
#[path = "../../cases/avian_ragdoll_stair_tumble_case.rs"]
pub mod avian_ragdoll_stair_tumble_case;
#[path = "../../cases/avian_large_pyramid_case.rs"]
pub mod avian_large_pyramid_case;

pub mod case_registry;
#[path = "../../../common/case_execution_wire.rs"]
pub mod case_execution_wire;
pub mod result_writer;
pub mod runner;
pub mod runner_args;
#[path = "../../../common/replay_recording.rs"]
pub mod replay_recording;

use std::process::ExitCode;

pub fn main() -> ExitCode
{
    runner::main_exit_code()
}
