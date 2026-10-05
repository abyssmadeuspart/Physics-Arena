use crate::runner_args::RunnerArgs;
use std::fs::{create_dir_all, read_to_string, rename, File, OpenOptions};
use std::io::Write;
use std::path::Path;
use std::time::Duration;

pub const CSV_HEADER: &str =
    "raw_schema_version,repeat_index,fixture_semantic,fixture_revision,physics_settings,body_count,shape_count,query_count,constraint_count,invalid_transform_count,case_status,metric_status,effective_thread_count,effective_worker_count,actual_taskgraph_worker_count,completed_work_unit_count,workload_elapsed_ms,render_elapsed_ms,present_wait_ms,visual_validation_status,proof_path\n";
pub const OBSERVATION_CSV_HEADER: &str = "repeat_index,metric_id,phase_id,sample_index,value\n";

#[derive(Clone, Copy)]
pub enum ResultValidity
{
    Invalid,
    Valid,
}

pub enum ObservationValue
{
    Uint64(u64),
    Float64(u64),
}

pub struct ObservationRow
{
    pub metric_id: &'static str,
    pub phase_id: &'static str,
    pub sample_index: u32,
    pub value: ObservationValue,
}

pub struct BenchmarkResult
{
    pub physics_settings: String,
    pub body_count: usize,
    pub shape_count: usize,
    pub query_count: usize,
    pub constraint_count: usize,
    pub invalid_transform_count: usize,
    pub effective_thread_count: usize,
    pub effective_worker_count: usize,
    pub completed_work_unit_count: usize,
    pub workload_elapsed_ms: f64,
    pub case_validity: ResultValidity,
    pub metric_validity: ResultValidity,
    pub observations: Vec<ObservationRow>,
}

pub fn write_step_timing(runner_args: &RunnerArgs, step_durations: &[Duration]) -> Result<(), i32>
{
    let Some(output_path_text) = &runner_args.step_timing_output_path else
    {
        return Ok(());
    };
    if step_durations.len() != runner_args.step_count
    {
        return Err(2);
    }
    let output_path: &Path = Path::new(output_path_text);
    if let Some(parent) = output_path.parent()
    {
        if !parent.as_os_str().is_empty()
        {
            create_dir_all(parent).map_err(|error|
            {
                eprintln!("result_failed reason=create_timing_dir path={} error={error}", parent.display());
                2
            })?;
        }
    }
    let temporary_path: std::path::PathBuf = output_path.with_extension("csv.tmp");
    let mut file: File = File::create(&temporary_path).map_err(|error|
    {
        eprintln!("result_failed reason=open_timing path={} error={error}", temporary_path.display());
        2
    })?;
    file.write_all(b"step_index,physics_step_ms,render_frame_ms\n").map_err(|_| 2)?;
    for (index, duration) in step_durations.iter().enumerate()
    {
        writeln!(file, "{},{:.9},", index + 1, duration.as_secs_f64() * 1000.0).map_err(|_| 2)?;
    }
    file.sync_all().map_err(|_| 2)?;
    drop(file);
    rename(&temporary_path, output_path).map_err(|error|
    {
        eprintln!("result_failed reason=publish_timing path={} error={error}", output_path.display());
        2
    })
}

pub fn write_result(
    runner_args: &RunnerArgs,
    result: &BenchmarkResult,
) -> Result<(), i32>
{
    write_observation_sidecar(runner_args, &result.observations)?;
    let output_path: &Path = Path::new(&runner_args.output_path);
    if let Some(parent) = output_path.parent()
    {
        if !parent.as_os_str().is_empty()
        {
            create_dir_all(parent).map_err(|error|
            {
                eprintln!(
                    "result_failed engine={} case={} reason=create_output_dir error={error}",
                    runner_args.case_registration.descriptor.engine_id,
                    crate::case_execution_wire::case_text(&runner_args.case_execution.case_id)
                );
                2
            })?;
        }
    }
    let write_header: bool = !output_path.is_file();
    let mut file: File = OpenOptions::new()
        .create(true)
        .append(true)
        .open(output_path)
        .map_err(|error|
        {
            eprintln!(
                "result_failed engine={} case={} reason=open_output error={error}",
                runner_args.case_registration.descriptor.engine_id,
                crate::case_execution_wire::case_text(&runner_args.case_execution.case_id)
            );
            2
        })?;
    if write_header
    {
        file.write_all(CSV_HEADER.as_bytes()).map_err(|_| 2)?;
    }
    let descriptor: crate::case_registry::RapierCaseDescriptor = runner_args.case_registration.descriptor;
    let case_status: &str = match result.case_validity
    {
        ResultValidity::Valid => "ok",
        ResultValidity::Invalid => "invalid_result",
    };
    let metric_status: &str = match (&result.case_validity, &result.metric_validity)
    {
        (ResultValidity::Valid, ResultValidity::Valid) => "ok",
        _ => "invalid_result",
    };
    let elapsed: String = if runner_args.case_execution.fixture_kind == crate::case_execution_wire::CaseFixtureKind::RagdollStairTumble
    {
        String::new()
    }
    else
    {
        format!("{:.9}", result.workload_elapsed_ms)
    };
    writeln!(
        file,
        "3,{},{},{},{},{},{},{},{},{},{},{},{},{},,{},{},,,,",
        runner_args.repeat_index,
        crate::case_execution_wire::case_text(&runner_args.case_execution.fixture_semantic),
        runner_args.case_execution.fixture_revision,
        result.physics_settings,
        result.body_count,
        result.shape_count,
        result.query_count,
        result.constraint_count,
        result.invalid_transform_count,
        case_status,
        metric_status,
        result.effective_thread_count,
        result.effective_worker_count,
        result.completed_work_unit_count,
        elapsed,
    )
    .map_err(|_| 2)?;
    file.flush().map_err(|_| 2)?;
    if metric_status != "ok"
    {
        eprintln!(
            "invalid_result engine={} case={} case_status={case_status} metric_status={metric_status} invalid={}",
            descriptor.engine_id,
            crate::case_execution_wire::case_text(&runner_args.case_execution.case_id),
            result.invalid_transform_count
        );
        return Err(2);
    }
    Ok(())
}

pub fn write_observation_sidecar(
    runner_args: &RunnerArgs,
    observations: &[ObservationRow],
) -> Result<(), i32>
{
    if observations.len() > 200
    {
        return Err(2);
    }
    let raw_path: &String = &runner_args.output_path;
    let Some(stem) = raw_path.strip_suffix("_raw.csv") else
    {
        return Err(2);
    };
    let observation_path_text: String = format!("{stem}_observations.csv");
    let observation_path: &Path = Path::new(&observation_path_text);
    if let Some(parent) = observation_path.parent()
    {
        if !parent.as_os_str().is_empty()
        {
            create_dir_all(parent).map_err(|_| 2)?;
        }
    }
    let existing: String = if observation_path.is_file()
    {
        let text: String = read_to_string(observation_path).map_err(|_| 2)?;
        if !text.starts_with(OBSERVATION_CSV_HEADER)
        {
            return Err(2);
        }
        text
    }
    else
    {
        OBSERVATION_CSV_HEADER.to_string()
    };
    for row in observations
    {
        if row.metric_id.is_empty()
            || row.phase_id.is_empty()
            || row.metric_id.chars().any(|c| matches!(c, ',' | '\r' | '\n'))
            || row.phase_id.chars().any(|c| matches!(c, ',' | '\r' | '\n'))
            || matches!(row.value, ObservationValue::Float64(bits) if !f64::from_bits(bits).is_finite())
        {
            return Err(2);
        }
    }
    let temporary_path: std::path::PathBuf = observation_path.with_extension("csv.tmp");
    let mut file: File = File::create(&temporary_path).map_err(|_| 2)?;
    file.write_all(existing.as_bytes()).map_err(|_| 2)?;
    for row in observations
    {
        match row.value
        {
            ObservationValue::Uint64(bits) => writeln!(
                file, "{},{},{},{},{}",
                runner_args.repeat_index, row.metric_id, row.phase_id, row.sample_index, bits
            ).map_err(|_| 2)?,
            ObservationValue::Float64(bits) =>
            {
                let value: f64 = f64::from_bits(bits);
                writeln!(
                    file, "{},{},{},{},{:.17}",
                    runner_args.repeat_index, row.metric_id, row.phase_id,
                    row.sample_index, value
                ).map_err(|_| 2)?;
            }
        }
    }
    file.sync_all().map_err(|_| 2)?;
    drop(file);
    rename(&temporary_path, observation_path).map_err(|_| 2)
}
