use crate::case_registry::{self, RapierCaseDescriptor};

pub struct RunnerArgs {
    pub case_descriptor: RapierCaseDescriptor,
    pub output_path: String,
    pub thread_count: usize,
    pub step_count: usize,
    pub warmup_steps: usize,
    pub repeat_index: usize,
}

pub fn default_args() -> RunnerArgs {
    RunnerArgs {
        case_descriptor: case_registry::default_case(),
        output_path: "polygon_results.csv".to_string(),
        thread_count: 1,
        step_count: 300,
        warmup_steps: 0,
        repeat_index: 0,
    }
}

pub fn parse_args<I>(args: I) -> Result<RunnerArgs, i32>
where
    I: Iterator<Item = String>,
{
    let mut runner_args = default_args();
    let mut requested_case_id = runner_args.case_descriptor.case_id.to_string();

    for arg in args {
        if let Some(value) = arg.strip_prefix("--case=") {
            requested_case_id = value.to_string();
        } else if let Some(value) = arg.strip_prefix("--thread-count=") {
            runner_args.thread_count = parse_number("thread-count", value, 1)?;
        } else if let Some(value) = arg.strip_prefix("--step-count=") {
            runner_args.step_count = parse_number("step-count", value, 1)?;
        } else if let Some(value) = arg.strip_prefix("--warmup-steps=") {
            runner_args.warmup_steps = parse_number("warmup-steps", value, 0)?;
        } else if let Some(value) = arg.strip_prefix("--repeat-index=") {
            runner_args.repeat_index = parse_number("repeat-index", value, 0)?;
        } else if let Some(value) = arg.strip_prefix("--output=") {
            runner_args.output_path = value.to_string();
        } else {
            eprintln!("invalid_argument value={arg}");
            return Err(2);
        }
    }

    runner_args.case_descriptor = case_registry::resolve(&requested_case_id)?;
    Ok(runner_args)
}

pub fn parse_number(name: &str, value: &str, minimum: usize) -> Result<usize, i32> {
    let parsed = match value.parse::<usize>() {
        Ok(number) => number,
        Err(_) => {
            eprintln!("invalid_argument name={name} value={value}");
            return Err(2);
        }
    };
    if parsed < minimum || parsed > 1_000_000 {
        eprintln!("invalid_argument name={name} value={value}");
        return Err(2);
    }
    Ok(parsed)
}
