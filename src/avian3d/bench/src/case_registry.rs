use crate::avian_box_container_pile_10k_case as box_container_pile;

#[derive(Clone, Copy)]
pub enum AvianCaseSlot {
    BoxContainerPile10K,
}

#[derive(Clone, Copy)]
pub struct AvianCaseDescriptor {
    pub slot: AvianCaseSlot,
    pub engine_id: &'static str,
    pub case_id: &'static str,
    pub engine_ref: &'static str,
    pub toolchain_id: &'static str,
    pub fixture_semantic: &'static str,
    pub fixture_version: &'static str,
    pub dynamic_body_count: usize,
    pub body_count: usize,
    pub static_body_count: usize,
    pub dynamic_half_extent: f32,
}

pub type AvianCaseWorld = box_container_pile::AvianWorld;
pub type AvianStabilityCounters = box_container_pile::StabilityCounters;

pub fn default_case() -> AvianCaseDescriptor {
    box_container_pile_descriptor()
}

pub fn resolve(case_id: &str) -> Result<AvianCaseDescriptor, i32> {
    if case_id == box_container_pile::CASE_ID {
        return Ok(box_container_pile_descriptor());
    }

    eprintln!("invalid_argument name=case value={case_id}");
    Err(2)
}

pub fn box_container_pile_descriptor() -> AvianCaseDescriptor {
    AvianCaseDescriptor {
        slot: AvianCaseSlot::BoxContainerPile10K,
        engine_id: box_container_pile::ENGINE_ID,
        case_id: box_container_pile::CASE_ID,
        engine_ref: box_container_pile::ENGINE_REF,
        toolchain_id: box_container_pile::TOOLCHAIN_ID,
        fixture_semantic: box_container_pile::FIXTURE_SEMANTIC,
        fixture_version: box_container_pile::FIXTURE_VERSION,
        dynamic_body_count: box_container_pile::DYNAMIC_BODY_COUNT,
        body_count: box_container_pile::BODY_COUNT,
        static_body_count: box_container_pile::STATIC_BODY_COUNT,
        dynamic_half_extent: box_container_pile::HALF_EXTENT,
    }
}

pub fn create_world(descriptor: AvianCaseDescriptor) -> Result<AvianCaseWorld, i32> {
    match descriptor.slot {
        AvianCaseSlot::BoxContainerPile10K => box_container_pile::create_world(),
    }
}

pub fn step_world(descriptor: AvianCaseDescriptor, world: &mut AvianCaseWorld, step_count: usize) {
    match descriptor.slot {
        AvianCaseSlot::BoxContainerPile10K => box_container_pile::step_world(world, step_count),
    }
}

pub fn stability_counters(
    descriptor: AvianCaseDescriptor,
    world: &AvianCaseWorld,
) -> AvianStabilityCounters {
    match descriptor.slot {
        AvianCaseSlot::BoxContainerPile10K => box_container_pile::stability_counters(world),
    }
}

pub fn case_status(descriptor: AvianCaseDescriptor, counters: AvianStabilityCounters) -> &'static str {
    match descriptor.slot {
        AvianCaseSlot::BoxContainerPile10K => box_container_pile::case_status(counters),
    }
}

pub fn metric_status(descriptor: AvianCaseDescriptor, case_status: &str) -> &'static str {
    match descriptor.slot {
        AvianCaseSlot::BoxContainerPile10K => box_container_pile::metric_status(case_status),
    }
}

pub fn host_route(descriptor: AvianCaseDescriptor) -> &'static str {
    match descriptor.slot {
        AvianCaseSlot::BoxContainerPile10K => box_container_pile::host_route(),
    }
}
