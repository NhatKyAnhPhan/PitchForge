//! Runtime-loaded FFI bindings to the PitchForge native engine C ABI
//! (`include/auralis/bridge/auralis_c_api.h`).
//!
//! The engine DLL is loaded with `LoadLibraryA`/`GetProcAddress` instead of a
//! link-time dependency, so the app also runs in a "demo mode" when the DLL
//! is missing: the UI is fully functional, meters stay at zero, and controls
//! become no-ops.

#![allow(non_camel_case_types, non_snake_case, dead_code)]

use std::ffi::{c_char, c_void};

pub const AURALIS_STATUS_OK: i32 = 0;
pub const AURALIS_STATUS_INVALID_ARGUMENT: i32 = 1;
pub const AURALIS_STATUS_DEVICE_ERROR: i32 = 2;
pub const AURALIS_STATUS_NOT_READY: i32 = 3;
pub const AURALIS_STATUS_QUEUE_FULL: i32 = 4;
pub const AURALIS_STATUS_INTERNAL_ERROR: i32 = 5;

pub const AURALIS_MAX_PARAMETERS_PER_EFFECT: usize = 16;
pub const AURALIS_MAX_EFFECTS_PER_CHANNEL: usize = 16;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct AuralisAudioConfig {
    pub sample_rate: f64,
    pub buffer_size: u32,
    pub input_channels: u32,
    pub output_channels: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct AuralisDeviceInfo {
    pub index: u32,
    pub name: [c_char; 192],
    pub input_channels: u32,
    pub output_channels: u32,
    pub is_default: u32,
}

impl AuralisDeviceInfo {
    pub fn name(&self) -> String {
        let mut name = String::new();
        for &byte in self.name.iter() {
            if byte == 0 {
                break;
            }
            name.push(byte as u8 as char);
        }
        name
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct AuralisMeters {
    pub input_peak_left: f32,
    pub input_peak_right: f32,
    pub input_rms_left: f32,
    pub input_rms_right: f32,
    pub output_peak_left: f32,
    pub output_peak_right: f32,
    pub output_rms_left: f32,
    pub output_rms_right: f32,
    pub clipping: u32,
    pub estimated_latency_ms: f64,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct AuralisMixerState {
    pub input_gain_db: f32,
    pub output_gain_db: f32,
    pub pan: f32,
    pub muted: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct AuralisEffectType(pub i32);

impl Default for AuralisEffectType {
    fn default() -> Self {
        AURALIS_EFFECT_NOISE_GATE
    }
}

impl AuralisEffectType {
    pub const NOISE_GATE: AuralisEffectType = AuralisEffectType(0);
    pub const EQUALIZER: AuralisEffectType = AuralisEffectType(1);
    pub const DE_ESSER: AuralisEffectType = AuralisEffectType(2);
    pub const COMPRESSOR: AuralisEffectType = AuralisEffectType(3);
    pub const PITCH_CORRECTION: AuralisEffectType = AuralisEffectType(4);
    pub const SATURATION: AuralisEffectType = AuralisEffectType(5);
    pub const REVERB: AuralisEffectType = AuralisEffectType(6);
    pub const DELAY: AuralisEffectType = AuralisEffectType(7);
    pub const LIMITER: AuralisEffectType = AuralisEffectType(8);

    pub fn name(&self) -> &'static str {
        match *self {
            AuralisEffectType::NOISE_GATE => "Noise Gate",
            AuralisEffectType::EQUALIZER => "Equalizer",
            AuralisEffectType::DE_ESSER => "De-Esser",
            AuralisEffectType::COMPRESSOR => "Compressor",
            AuralisEffectType::PITCH_CORRECTION => "Pitch Correction",
            AuralisEffectType::SATURATION => "Saturation",
            AuralisEffectType::REVERB => "Reverb",
            AuralisEffectType::DELAY => "Delay",
            AuralisEffectType::LIMITER => "Limiter",
            _ => "Unknown",
        }
    }

    pub fn short_name(&self) -> &'static str {
        match *self {
            AuralisEffectType::NOISE_GATE => "NoiseGate",
            AuralisEffectType::EQUALIZER => "Equalizer",
            AuralisEffectType::DE_ESSER => "DeEsser",
            AuralisEffectType::COMPRESSOR => "Compressor",
            AuralisEffectType::PITCH_CORRECTION => "PitchCorrection",
            AuralisEffectType::SATURATION => "Saturation",
            AuralisEffectType::REVERB => "Reverb",
            AuralisEffectType::DELAY => "Delay",
            AuralisEffectType::LIMITER => "Limiter",
            _ => "Effect",
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct AuralisEffectState {
    pub instance_id: u32,
    pub ty: i32,
    pub enabled: u32,
    pub wet: f32,
    pub parameter_count: u32,
    pub parameter_ids: [u16; AURALIS_MAX_PARAMETERS_PER_EFFECT],
    pub parameter_values: [f32; AURALIS_MAX_PARAMETERS_PER_EFFECT],
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct AuralisChainState {
    pub effect_count: u32,
    pub effects: [AuralisEffectState; AURALIS_MAX_EFFECTS_PER_CHANNEL],
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct AuralisPitchDiagnostics {
    pub detected_frequency_hz: f32,
    pub target_frequency_hz: f32,
    pub confidence: f32,
    pub applied_shift_semitones: f32,
}

// Free-const aliases for the ABI effect types (ergonomic Rust use).
pub const AURALIS_EFFECT_NOISE_GATE: AuralisEffectType = AuralisEffectType::NOISE_GATE;
pub const AURALIS_EFFECT_EQUALIZER: AuralisEffectType = AuralisEffectType::EQUALIZER;
pub const AURALIS_EFFECT_DE_ESSER: AuralisEffectType = AuralisEffectType::DE_ESSER;
pub const AURALIS_EFFECT_COMPRESSOR: AuralisEffectType = AuralisEffectType::COMPRESSOR;
pub const AURALIS_EFFECT_PITCH_CORRECTION: AuralisEffectType = AuralisEffectType::PITCH_CORRECTION;
pub const AURALIS_EFFECT_SATURATION: AuralisEffectType = AuralisEffectType::SATURATION;
pub const AURALIS_EFFECT_REVERB: AuralisEffectType = AuralisEffectType::REVERB;
pub const AURALIS_EFFECT_DELAY: AuralisEffectType = AuralisEffectType::DELAY;
pub const AURALIS_EFFECT_LIMITER: AuralisEffectType = AuralisEffectType::LIMITER;

/// Opaque engine handle (`AuralisEngine*`).
pub type EngineHandle = *mut c_void;

#[derive(Clone, Copy)]
pub struct EngineApi {
    pub api_version: extern "system" fn() -> u32,
    pub engine_create: extern "system" fn() -> EngineHandle,
    pub engine_destroy: extern "system" fn(EngineHandle),
    pub engine_start: extern "system" fn(EngineHandle, &AuralisAudioConfig) -> i32,
    pub engine_stop: extern "system" fn(EngineHandle),
    pub engine_is_running: extern "system" fn(EngineHandle) -> u32,
    pub engine_get_meters: extern "system" fn(EngineHandle, &mut AuralisMeters) -> i32,
    pub engine_get_mixer_state: extern "system" fn(EngineHandle, &mut AuralisMixerState) -> i32,
    pub engine_set_input_gain: extern "system" fn(EngineHandle, f32) -> i32,
    pub engine_set_output_gain: extern "system" fn(EngineHandle, f32) -> i32,
    pub engine_set_pan: extern "system" fn(EngineHandle, f32) -> i32,
    pub engine_set_mute: extern "system" fn(EngineHandle, u32) -> i32,
    pub device_count: extern "system" fn(u32) -> u32,
    pub device_get_info: extern "system" fn(u32, u32, &mut AuralisDeviceInfo) -> i32,
    pub engine_select_device: extern "system" fn(EngineHandle, u32, u32) -> i32,
    pub engine_effect_add: extern "system" fn(EngineHandle, i32, u32, u32) -> i32,
    pub engine_effect_remove: extern "system" fn(EngineHandle, u32) -> i32,
    pub engine_effect_move: extern "system" fn(EngineHandle, u32, u32) -> i32,
    pub engine_effect_set_enabled: extern "system" fn(EngineHandle, u32, u32) -> i32,
    pub engine_effect_set_wet: extern "system" fn(EngineHandle, u32, f32) -> i32,
    pub engine_effect_set_parameter:
        extern "system" fn(EngineHandle, u32, u16, f32) -> i32,
    pub engine_get_chain_state: extern "system" fn(EngineHandle, &mut AuralisChainState) -> i32,
    /// Proposed C ABI addition (see API_CONTRACTS.md): pitch-correction
    /// telemetry for the pitch display. `None` on older DLLs.
    pub engine_get_pitch_diagnostics: Option<
        extern "system" fn(EngineHandle, u32, &mut AuralisPitchDiagnostics) -> i32,
    >,
}

#[link(name = "kernel32")]
extern "system" {
    fn LoadLibraryA(name: *const c_char) -> *mut c_void;
    fn GetProcAddress(module: *mut c_void, name: *const c_char) -> Option<*mut c_void>;
}

fn load_symbol<T>(module: *mut c_void, name: &str) -> Option<T> {
    let cname = std::ffi::CString::new(name).ok()?;
    let ptr = unsafe { GetProcAddress(module, cname.as_ptr()) }?;
    Some(unsafe { std::mem::transmute_copy::<*mut c_void, T>(&ptr) })
}

pub const ENGINE_DLL_NAME: &str = "auralis_audio_engine.dll";

pub struct Engine {
    module: *mut c_void,
    handle: EngineHandle,
    pub api: EngineApi,
}

unsafe impl Send for Engine {}
unsafe impl Sync for Engine {}

impl Engine {
    pub fn load() -> Option<Engine> {
        let cname = std::ffi::CString::new(ENGINE_DLL_NAME).ok()?;
        let module = unsafe { LoadLibraryA(cname.as_ptr()) };
        if module.is_null() {
            return None;
        }
        let api = EngineApi {
            api_version: load_symbol(module, "auralis_api_version")?,
            engine_create: load_symbol(module, "auralis_engine_create")?,
            engine_destroy: load_symbol(module, "auralis_engine_destroy")?,
            engine_start: load_symbol(module, "auralis_engine_start")?,
            engine_stop: load_symbol(module, "auralis_engine_stop")?,
            engine_is_running: load_symbol(module, "auralis_engine_is_running")?,
            engine_get_meters: load_symbol(module, "auralis_engine_get_meters")?,
            engine_get_mixer_state: load_symbol(module, "auralis_engine_get_mixer_state")?,
            engine_set_input_gain: load_symbol(module, "auralis_engine_set_input_gain")?,
            engine_set_output_gain: load_symbol(module, "auralis_engine_set_output_gain")?,
            engine_set_pan: load_symbol(module, "auralis_engine_set_pan")?,
            engine_set_mute: load_symbol(module, "auralis_engine_set_mute")?,
            device_count: load_symbol(module, "auralis_device_count")?,
            device_get_info: load_symbol(module, "auralis_device_get_info")?,
            engine_select_device: load_symbol(module, "auralis_engine_select_device")?,
            engine_effect_add: load_symbol(module, "auralis_engine_effect_add")?,
            engine_effect_remove: load_symbol(module, "auralis_engine_effect_remove")?,
            engine_effect_move: load_symbol(module, "auralis_engine_effect_move")?,
            engine_effect_set_enabled:
                load_symbol(module, "auralis_engine_effect_set_enabled")?,
            engine_effect_set_wet: load_symbol(module, "auralis_engine_effect_set_wet")?,
            engine_effect_set_parameter:
                load_symbol(module, "auralis_engine_effect_set_parameter")?,
            engine_get_chain_state: load_symbol(module, "auralis_engine_get_chain_state")?,
            engine_get_pitch_diagnostics:
                load_symbol(module, "auralis_engine_get_pitch_diagnostics"),
        };
        let handle = (api.engine_create)();
        if handle.is_null() {
            return None;
        }
        Some(Engine { module, handle, api })
    }

    pub fn api_version(&self) -> u32 {
        unsafe { (self.api.api_version)() }
    }

    pub fn handle(&self) -> EngineHandle {
        self.handle
    }

    pub fn start(&self, config: &AuralisAudioConfig) -> i32 {
        unsafe { (self.api.engine_start)(self.handle, config) }
    }

    pub fn stop(&self) {
        unsafe { (self.api.engine_stop)(self.handle) }
    }

    pub fn is_running(&self) -> u32 {
        unsafe { (self.api.engine_is_running)(self.handle) }
    }

    pub fn meters(&self) -> Result<AuralisMeters, i32> {
        let mut meters = AuralisMeters::default();
        let status = unsafe { (self.api.engine_get_meters)(self.handle, &mut meters) };
        if status == AURALIS_STATUS_OK {
            Ok(meters)
        } else {
            Err(status)
        }
    }

    pub fn mixer_state(&self) -> Result<AuralisMixerState, i32> {
        let mut state = AuralisMixerState::default();
        let status = unsafe { (self.api.engine_get_mixer_state)(self.handle, &mut state) };
        if status == AURALIS_STATUS_OK {
            Ok(state)
        } else {
            Err(status)
        }
    }

    pub fn set_input_gain(&self, gain_db: f32) -> i32 {
        unsafe { (self.api.engine_set_input_gain)(self.handle, gain_db) }
    }

    pub fn set_output_gain(&self, gain_db: f32) -> i32 {
        unsafe { (self.api.engine_set_output_gain)(self.handle, gain_db) }
    }

    pub fn set_pan(&self, pan: f32) -> i32 {
        unsafe { (self.api.engine_set_pan)(self.handle, pan) }
    }

    pub fn set_mute(&self, muted: u32) -> i32 {
        unsafe { (self.api.engine_set_mute)(self.handle, muted) }
    }

    pub fn device_count(&self, for_input: u32) -> u32 {
        unsafe { (self.api.device_count)(for_input) }
    }

    pub fn device_get_info(&self, for_input: u32, index: u32) -> Option<AuralisDeviceInfo> {
        let mut info = AuralisDeviceInfo {
            index: 0,
            name: [0; 192],
            input_channels: 0,
            output_channels: 0,
            is_default: 0,
        };
        let status =
            unsafe { (self.api.device_get_info)(for_input, index, &mut info) };
        if status == AURALIS_STATUS_OK {
            Some(info)
        } else {
            None
        }
    }

    pub fn select_device(&self, for_input: u32, index: u32) -> i32 {
        unsafe { (self.api.engine_select_device)(self.handle, for_input, index) }
    }

    pub fn effect_add(&self, ty: AuralisEffectType, instance_id: u32, index: u32) -> i32 {
        unsafe {
            (self.api.engine_effect_add)(self.handle, ty.0, instance_id, index)
        }
    }

    pub fn effect_remove(&self, instance_id: u32) -> i32 {
        unsafe { (self.api.engine_effect_remove)(self.handle, instance_id) }
    }

    pub fn effect_move(&self, instance_id: u32, destination: u32) -> i32 {
        unsafe { (self.api.engine_effect_move)(self.handle, instance_id, destination) }
    }

    pub fn effect_set_enabled(&self, instance_id: u32, enabled: u32) -> i32 {
        unsafe { (self.api.engine_effect_set_enabled)(self.handle, instance_id, enabled) }
    }

    pub fn effect_set_wet(&self, instance_id: u32, wet: f32) -> i32 {
        unsafe { (self.api.engine_effect_set_wet)(self.handle, instance_id, wet) }
    }

    pub fn effect_set_parameter(&self, instance_id: u32, pid: u16, value: f32) -> i32 {
        unsafe { (self.api.engine_effect_set_parameter)(self.handle, instance_id, pid, value) }
    }

    pub fn get_chain_state(&self) -> Result<AuralisChainState, i32> {
        let mut state = AuralisChainState::default();
        let status = unsafe { (self.api.engine_get_chain_state)(self.handle, &mut state) };
        if status == AURALIS_STATUS_OK {
            Ok(state)
        } else {
            Err(status)
        }
    }

    /// Pitch-correction telemetry for the pitch display (proposed C ABI).
    pub fn get_pitch_diagnostics(&self, instance_id: u32) -> Option<Result<AuralisPitchDiagnostics, i32>> {
        let getter = (self.api.engine_get_pitch_diagnostics)?;
        let mut diagnostics = AuralisPitchDiagnostics::default();
        let status = unsafe { getter(self.handle, instance_id, &mut diagnostics) };
        if status == AURALIS_STATUS_OK {
            Some(Ok(diagnostics))
        } else if status == AURALIS_STATUS_NOT_READY {
            None
        } else {
            Some(Err(status))
        }
    }
}

impl Drop for Engine {
    fn drop(&mut self) {
        unsafe { (self.api.engine_destroy)(self.handle) }
    }
}
