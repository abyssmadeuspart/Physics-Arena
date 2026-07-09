use crate::case_registry;
use crate::result_writer;
use crate::runner_args;
use crate::shared_visual_mode;
use bevy::tasks::{ComputeTaskPool, TaskPoolBuilder};
use std::env;
use std::process::ExitCode;
use std::time::Instant;

pub enum AvianRunnerMode {
    Headless,
    SharedVisual,
}

pub fn main_exit_code() -> ExitCode {
    let args: Vec<String> = env::args().skip(1).collect();
    let result = match select_runner_mode(&args) {
        AvianRunnerMode::Headless => run(args.into_iter()),
        AvianRunnerMode::SharedVisual => shared_visual_mode::run(args.into_iter()).map_err(|code| code as i32),
    };
    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(code) => ExitCode::from(code as u8),
    }
}

pub fn select_runner_mode(args: &[String]) -> AvianRunnerMode {
    for arg in args {
        if arg.starts_with("--producer-mode=") {
            return AvianRunnerMode::SharedVisual;
        }
    }
    AvianRunnerMode::Headless
}

pub fn run<I>(args: I) -> Result<(), i32>
where
    I: Iterator<Item = String>,
{
    let runner_args = runner_args::parse_args(args)?;
    let case_descriptor = runner_args.case_descriptor;
    initialize_worker_pools(runner_args.thread_count)?;
    let effective_thread_count = ComputeTaskPool::get().thread_num();
    let effective_rayon_thread_count = rayon::current_num_threads();
    if effective_thread_count != runner_args.thread_count ||
        effective_rayon_thread_count != runner_args.thread_count
    {
        eprintln!(
            "run_failed reason=thread_pool_mismatch requested={} effective_compute={} effective_rayon={}",
            runner_args.thread_count, effective_thread_count, effective_rayon_thread_count
        );
        return Err(2);
    }

    if runner_args.warmup_steps > 0 {
        let mut warmup_world = case_registry::create_world(case_descriptor)?;
        case_registry::step_world(case_descriptor, &mut warmup_world, runner_args.warmup_steps);
    }

    let mut world = case_registry::create_world(case_descriptor)?;
    let start = Instant::now();
    case_registry::step_world(case_descriptor, &mut world, runner_args.step_count);
    let elapsed_ms = start.elapsed().as_secs_f64() * 1000.0;
    result_writer::write_result(&runner_args, &world, elapsed_ms, effective_thread_count)?;
    Ok(())
}

pub fn initialize_worker_pools(thread_count: usize) -> Result<(), i32> {
    ComputeTaskPool::get_or_init(|| TaskPoolBuilder::new().num_threads(thread_count).build());
    if let Err(error) = rayon::ThreadPoolBuilder::new()
        .num_threads(thread_count)
        .build_global()
    {
        eprintln!("run_failed reason=rayon_thread_pool error={error}");
        return Err(2);
    }
    Ok(())
}
