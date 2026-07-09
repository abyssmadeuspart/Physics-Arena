use crate::case_registry::{self, RapierCaseWorld};
use crate::runner_args::RunnerArgs;
use std::fs::{create_dir_all, OpenOptions};
use std::io::Write;
use std::path::Path;

pub const CSV_HEADER: &str =
    "body_count,shape_count,invalid_transform_count,below_floor_count,out_of_bounds_count,case_status,metric_status,\
effective_thread_count,effective_worker_count,physics_elapsed_ms\n";

pub fn write_result(
    runner_args: &RunnerArgs,
    world: &RapierCaseWorld,
    elapsed_ms: f64,
    effective_thread_count: usize,
) -> Result<(), i32> {
    let output_path = Path::new(&runner_args.output_path);
    if let Some(parent) = output_path.parent() {
        if !parent.as_os_str().is_empty() {
            if let Err(error) = create_dir_all(parent) {
                eprintln!("result_failed reason=create_output_dir path={} error={error}", parent.display());
                return Err(2);
            }
        }
    }

    let write_header = if output_path.is_file() { 0 } else { 1 };
    let mut file = match OpenOptions::new().create(true).append(true).open(output_path) {
        Ok(file) => file,
        Err(error) => {
            eprintln!("result_failed reason=open_output path={} error={error}", output_path.display());
            return Err(2);
        }
    };

    if write_header != 0 {
        if let Err(error) = file.write_all(CSV_HEADER.as_bytes()) {
            eprintln!("result_failed reason=write_header path={} error={error}", output_path.display());
            return Err(2);
        }
    }

    let case_descriptor = runner_args.case_descriptor;
    let counters = case_registry::stability_counters(case_descriptor, world);
    let ms_per_step = elapsed_ms / runner_args.step_count as f64;
    let steps_per_second = 1000.0 / ms_per_step;
    let case_status = case_registry::case_status(case_descriptor, counters);
    let metric_status = case_registry::metric_status(case_descriptor, case_status);
    if elapsed_ms <= 0.0 ||
        !elapsed_ms.is_finite() ||
        ms_per_step <= 0.0 ||
        !ms_per_step.is_finite() ||
        steps_per_second <= 0.0 ||
        !steps_per_second.is_finite()
    {
        eprintln!(
            "invalid_result reason=metric elapsed_ms={:.9} step_count={}",
            elapsed_ms,
            runner_args.step_count
        );
        return Err(2);
    }
    if metric_status != "ok" {
        eprintln!("invalid_result reason=case_status status={case_status}");
        return Err(2);
    }

    let line = format!(
        "{},{},{},{},{},{},{},{},{},{:.9}\n",
        world.rigid_body_set.len(),
        world.collider_set.len(),
        counters.invalid_transform_count,
        counters.below_floor_count,
        counters.out_of_bounds_count,
        case_status,
        metric_status,
        effective_thread_count,
        effective_thread_count,
        elapsed_ms
    );
    if let Err(error) = file.write_all(line.as_bytes()) {
        eprintln!("result_failed reason=write_row path={} error={error}", output_path.display());
        return Err(2);
    }
    Ok(())
}
