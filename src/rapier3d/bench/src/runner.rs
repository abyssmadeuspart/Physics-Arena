use crate::case_registry;
use crate::result_writer;
use crate::runner_args;
use crate::shared_visual_mode;
use std::env;
use std::process::ExitCode;
use std::time::Instant;

pub enum RapierRunnerMode {
    Headless,
    SharedVisual,
}

pub fn main_exit_code() -> ExitCode {
    let args: Vec<String> = env::args().skip(1).collect();
    let result = match select_runner_mode(&args) {
        RapierRunnerMode::Headless => run(args.into_iter()),
        RapierRunnerMode::SharedVisual => shared_visual_mode::run(args.into_iter()).map_err(|code| code as i32),
    };
    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(code) => ExitCode::from(code as u8),
    }
}

pub fn select_runner_mode(args: &[String]) -> RapierRunnerMode {
    for arg in args {
        if arg.starts_with("--producer-mode=") {
            return RapierRunnerMode::SharedVisual;
        }
    }
    RapierRunnerMode::Headless
}

pub fn run<I>(args: I) -> Result<(), i32>
where
    I: Iterator<Item = String>,
{
    let runner_args = runner_args::parse_args(args)?;
    let case_descriptor = runner_args.case_descriptor;
    if let Err(error) = rapier3d::rayon::ThreadPoolBuilder::new()
        .num_threads(runner_args.thread_count)
        .build_global()
    {
        eprintln!("run_failed reason=thread_pool error={error}");
        return Err(2);
    }
    let effective_thread_count = rapier3d::rayon::current_num_threads();
    if effective_thread_count != runner_args.thread_count {
        eprintln!(
            "run_failed reason=thread_pool_mismatch requested={} effective={}",
            runner_args.thread_count, effective_thread_count
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
