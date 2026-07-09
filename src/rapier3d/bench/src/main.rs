#[path = "../../cases/rapier_box_container_pile_10k_case.rs"]
pub mod rapier_box_container_pile_10k_case;

pub mod case_registry;
pub mod result_writer;
pub mod runner;
pub mod runner_args;
pub mod shared_visual_mode;

use std::process::ExitCode;

pub fn main() -> ExitCode {
    runner::main_exit_code()
}
