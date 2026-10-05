use crate::runner_args;
use std::env;
use std::process::ExitCode;

pub fn main_exit_code() -> ExitCode
{
    let args: Vec<String> = env::args().skip(1).collect();
    let result: Result<(), i32> = run(args.into_iter());
    match result
    {
        Ok(()) => ExitCode::SUCCESS,
        Err(code) => ExitCode::from(code as u8),
    }
}

pub fn run<I>(args: I) -> Result<(), i32>
where
    I: Iterator<Item = String>,
{
    let runner_args: runner_args::RunnerArgs = runner_args::parse_args(args)?;
    if let Err(error) = rapier3d::rayon::ThreadPoolBuilder::new()
        .num_threads(runner_args.thread_count)
        .build_global()
    {
        eprintln!("run_failed reason=thread_pool error={error}");
        return Err(2);
    }
    let effective_thread_count: usize = rapier3d::rayon::current_num_threads();
    if effective_thread_count != runner_args.thread_count
    {
        eprintln!(
            "run_failed reason=thread_pool_mismatch requested={} effective={}",
            runner_args.thread_count, effective_thread_count
        );
        return Err(2);
    }

    (runner_args.case_registration.run_headless)(&runner_args, effective_thread_count)
}
