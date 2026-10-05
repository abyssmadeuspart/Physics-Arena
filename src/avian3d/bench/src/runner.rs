use crate::case_execution_wire::CaseFixtureKind;
use crate::runner_args;
use bevy::tasks::{ComputeTaskPool, TaskPoolBuilder};
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
    let effective_thread_count: usize = initialize_worker_pools(
        runner_args.thread_count, runner_args.case_execution.fixture_kind)?;
    (runner_args.case_registration.run_headless)(&runner_args, effective_thread_count)
}

pub fn initialize_worker_pools(
    thread_count: usize,
    fixture_kind: CaseFixtureKind,
) -> Result<usize, i32>
{
    ComputeTaskPool::get_or_init(|| TaskPoolBuilder::new().num_threads(thread_count).build());
    let effective_thread_count: usize = ComputeTaskPool::get().thread_num();
    if effective_thread_count != thread_count
    {
        eprintln!(
            "run_failed reason=thread_pool_mismatch requested={thread_count} effective_compute={effective_thread_count}"
        );
        return Err(2);
    }
    if fixture_kind == CaseFixtureKind::SpatialQueryTrace && thread_count > 1
    {
        if let Err(error) = rayon::ThreadPoolBuilder::new()
            .num_threads(thread_count)
            .build_global()
        {
            eprintln!("run_failed reason=rayon_thread_pool error={error}");
            return Err(2);
        }
        let effective_rayon_thread_count: usize = rayon::current_num_threads();
        if effective_rayon_thread_count != thread_count
        {
            eprintln!(
                "run_failed reason=thread_pool_mismatch requested={thread_count} effective_rayon={effective_rayon_thread_count}"
            );
            return Err(2);
        }
    }
    Ok(effective_thread_count)
}
