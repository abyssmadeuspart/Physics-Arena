use crate::rapier_box_container_pile_10k_case as box_container_pile;

#[derive(Clone, Copy)]
pub enum RapierCaseSlot {
    BoxContainerPile10K,
}

#[derive(Clone, Copy)]
pub struct RapierCaseDescriptor {
    pub slot: RapierCaseSlot,
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

pub type RapierCaseWorld = box_container_pile::RapierWorld;
pub type RapierStabilityCounters = box_container_pile::StabilityCounters;

pub fn default_case() -> RapierCaseDescriptor {
    box_container_pile_descriptor()
}

pub fn resolve(case_id: &str) -> Result<RapierCaseDescriptor, i32> {
    if case_id == box_container_pile::CASE_ID {
        return Ok(box_container_pile_descriptor());
    }

    eprintln!("invalid_argument name=case value={case_id}");
    Err(2)
}

pub fn box_container_pile_descriptor() -> RapierCaseDescriptor {
    RapierCaseDescriptor {
        slot: RapierCaseSlot::BoxContainerPile10K,
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

pub fn create_world(descriptor: RapierCaseDescriptor) -> Result<RapierCaseWorld, i32> {
    match descriptor.slot {
        RapierCaseSlot::BoxContainerPile10K => box_container_pile::create_world(),
    }
}

pub fn step_world(descriptor: RapierCaseDescriptor, world: &mut RapierCaseWorld, step_count: usize) {
    match descriptor.slot {
        RapierCaseSlot::BoxContainerPile10K => box_container_pile::step_world(world, step_count),
    }
}

pub fn run_warmup(descriptor: RapierCaseDescriptor, step_count: usize) -> i32 {
    match create_world(descriptor) {
        Ok(mut world) => {
            step_world(descriptor, &mut world, step_count);
            0
        }
        Err(code) => code,
    }
}

pub fn sample_transforms(
    descriptor: RapierCaseDescriptor,
    world: &RapierCaseWorld,
    transforms: &mut [box_container_pile::RapierTransform],
) -> Result<(), i32> {
    match descriptor.slot {
        RapierCaseSlot::BoxContainerPile10K => box_container_pile::sample_transforms(world, transforms),
    }
}

pub fn copy_static_boxes(
    descriptor: RapierCaseDescriptor,
    boxes: &mut [box_container_pile::RapierStaticBox],
) -> Result<(), i32> {
    match descriptor.slot {
        RapierCaseSlot::BoxContainerPile10K => box_container_pile::copy_static_boxes(boxes),
    }
}

pub fn stability_counters(
    descriptor: RapierCaseDescriptor,
    world: &RapierCaseWorld,
) -> RapierStabilityCounters {
    match descriptor.slot {
        RapierCaseSlot::BoxContainerPile10K => box_container_pile::stability_counters(world),
    }
}

pub fn case_status(descriptor: RapierCaseDescriptor, counters: RapierStabilityCounters) -> &'static str {
    match descriptor.slot {
        RapierCaseSlot::BoxContainerPile10K => box_container_pile::case_status(counters),
    }
}

pub fn metric_status(descriptor: RapierCaseDescriptor, case_status: &str) -> &'static str {
    match descriptor.slot {
        RapierCaseSlot::BoxContainerPile10K => box_container_pile::metric_status(case_status),
    }
}

pub fn host_route(descriptor: RapierCaseDescriptor) -> &'static str {
    match descriptor.slot {
        RapierCaseSlot::BoxContainerPile10K => box_container_pile::host_route(),
    }
}
