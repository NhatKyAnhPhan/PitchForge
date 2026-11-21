//! Application state for the PitchForge Rust UI.
//!
//! Pure data types + engine polling helpers; the Slint UI consumes these
//! through exported properties and models (see `src/main.rs`).
//!
//! Display ranges mirror `native/src/dsp/Processors.cpp` (normalized
//! parameter → physical units) and `native/src/audio/AudioEngine.cpp`.

use crate::ffi::*;
use std::sync::atomic::{AtomicU32, Ordering};

static NEXT_EFFECT_INSTANCE: AtomicU32 = AtomicU32::new(10_000);

pub const INPUT_GAIN_MIN_DB: f32 = -60.0;
pub const INPUT_GAIN_MAX_DB: f32 = 24.0;
pub const OUTPUT_GAIN_MIN_DB: f32 = -60.0;
pub const OUTPUT_GAIN_MAX_DB: f32 = 12.0;
pub const PAN_MIN: f32 = -1.0;
pub const PAN_MAX: f32 = 1.0;

pub fn status_name(status: i32) -> &'static str {
    match status {
        AURALIS_STATUS_OK => "OK",
        AURALIS_STATUS_INVALID_ARGUMENT => "Invalid argument",
        AURALIS_STATUS_DEVICE_ERROR => "Device error",
        AURALIS_STATUS_NOT_READY => "Not ready",
        AURALIS_STATUS_QUEUE_FULL => "Queue full",
        AURALIS_STATUS_INTERNAL_ERROR => "Internal error",
        _ => "Unknown error",
    }
}

pub fn db_text(db: f32) -> String {
    format!("{:+.1} dB", db)
}

pub fn hz_text(hz: f32) -> String {
    if hz >= 1000.0 {
        format!("{:.2} kHz", hz / 1000.0)
    } else {
        format!("{:.1} Hz", hz)
    }
}

pub fn ms_text(ms: f32) -> String {
    if ms >= 1000.0 {
        format!("{:.2} s", ms / 1000.0)
    } else {
        format!("{:.1} ms", ms)
    }
}

pub fn s_text(seconds: f32) -> String {
    if seconds >= 1.0 {
        format!("{:.2} s", seconds)
    } else {
        format!("{:.3} s", seconds)
    }
}

pub fn latency_text(ms: f64) -> String {
    format!("{:.2} ms", ms)
}

/// Physical-unit label for a chain parameter (mirrors native/src/dsp/Processors.cpp).
pub fn effect_param_text(ty: AuralisEffectType, pid: u16, value: f32) -> String {
    let v = value.clamp(0.0, 1.0);
    let f = |min: f32, max: f32| v * (max - min) + min; // linear map
    let log = |min: f32, max: f32| min * (max / min).powf(v); // log map (Processors.cpp)
    match ty {
        AuralisEffectType::EQUALIZER => match pid {
            0 => hz_text(log(20.0, 20000.0)),
            1 => db_text(f(-18.0, 18.0)),
            2 => format!("{:.2}", log(0.25, 12.0)),
            3 => match (v * 3.0).round() as i32 {
                0 => "Peak".to_string(),
                1 => "High-Pass".to_string(),
                2 => "Low-Pass".to_string(),
                _ => "Bypass".to_string(),
            },
            4 | 5 => hz_text(log(20.0, 20000.0)),
            _ => format!("{:.2}", v),
        },
        AuralisEffectType::DE_ESSER => match pid {
            0 => hz_text(log(2000.0, 12000.0)),
            1 => db_text(f(-60.0, -6.0)),
            2 => format!("{:.2}", f(1.0, 12.0)),
            3 => ms_text(log(0.1, 20.0)),
            4 => ms_text(log(20.0, 500.0)),
            _ => format!("{:.2}", v),
        },
        AuralisEffectType::COMPRESSOR => match pid {
            0 => db_text(f(-60.0, 0.0)),
            1 => format!("{:.2}", f(1.0, 20.0)),
            2 => ms_text(log(0.1, 100.0)),
            3 => ms_text(log(10.0, 1000.0)),
            4 => db_text(f(0.0, 24.0)),
            5 => db_text(f(0.0, 24.0)),
            _ => format!("{:.2}", v),
        },
        AuralisEffectType::NOISE_GATE => match pid {
            0 => db_text(f(-80.0, -10.0)),
            1 => ms_text(log(0.1, 100.0)),
            2 => ms_text(log(10.0, 1000.0)),
            3 => ms_text(v * 0.5),
            4 => format!("{:.2}", v),
            _ => format!("{:.2}", v),
        },
        AuralisEffectType::LIMITER => match pid {
            0 => db_text(f(-36.0, 0.0)),
            1 => ms_text(log(10.0, 1000.0)),
            2 => db_text(f(-12.0, 0.0)),
            _ => format!("{:.2}", v),
        },
        AuralisEffectType::SATURATION => match pid {
            0 => format!("{:.2}", f(1.0, 20.0)),
            1 => hz_text(log(1000.0, 18000.0)),
            2 => db_text(f(-12.0, 12.0)),
            _ => format!("{:.2}", v),
        },
        AuralisEffectType::REVERB => match pid {
            0 => match (v * 3.0).round() as i32 {
                0 => "Room".to_string(),
                1 => "Small Hall".to_string(),
                2 => "Medium Hall".to_string(),
                _ => "Large Hall".to_string(),
            },
            1 => s_text(log(0.2, 8.0)),
            2 => ms_text(v * 200.0),
            3 => format!("{:.2}", f(0.5, 1.5)),
            4 => format!("{:.2}", v),
            5 => format!("{:.2}", f(0.0, 1.6)),
            _ => format!("{:.2}", v),
        },
        AuralisEffectType::DELAY => match pid {
            0 => s_text(log(0.01, 2.0)),
            1 => format!("{:.2}", v * 0.92),
            2 => format!("{:.2}", v),
            3 => format!("{:.2}", (1.0 - v) * 0.95),
            _ => format!("{:.2}", v),
        },
        AuralisEffectType::PITCH_CORRECTION => match pid {
            0 => format!("Note {}", (v * 11.0).round() as i32),
            1 => match (v * 3.0).round() as i32 {
                0 => "Chromatic".to_string(),
                1 => "Major".to_string(),
                2 => "Minor".to_string(),
                _ => "Custom".to_string(),
            },
            2 | 3 | 4 | 5 | 6 | 7 | 8 => format!("{:.2}", v),
            32..=43 => format!("{:.2}", v),
            _ => format!("{:.2}", v),
        },
        _ => format!("{:.2}", v),
    }
}

/// Human-readable parameter label (independent of the value).
pub fn effect_param_label(ty: AuralisEffectType, pid: u16) -> &'static str {
    match (ty, pid) {
        (AURALIS_EFFECT_EQUALIZER, 0) => "Frequency",
        (AURALIS_EFFECT_EQUALIZER, 1) => "Gain",
        (AURALIS_EFFECT_EQUALIZER, 2) => "Q",
        (AURALIS_EFFECT_EQUALIZER, 3) => "Filter",
        (AURALIS_EFFECT_EQUALIZER, 4) => "High-Pass",
        (AURALIS_EFFECT_EQUALIZER, 5) => "Low-Pass",
        (AURALIS_EFFECT_DE_ESSER, 0) => "Frequency",
        (AURALIS_EFFECT_DE_ESSER, 1) => "Threshold",
        (AURALIS_EFFECT_DE_ESSER, 2) => "Ratio",
        (AURALIS_EFFECT_DE_ESSER, 3) => "Attack",
        (AURALIS_EFFECT_DE_ESSER, 4) => "Release",
        (AURALIS_EFFECT_COMPRESSOR, 0) => "Threshold",
        (AURALIS_EFFECT_COMPRESSOR, 1) => "Ratio",
        (AURALIS_EFFECT_COMPRESSOR, 2) => "Attack",
        (AURALIS_EFFECT_COMPRESSOR, 3) => "Release",
        (AURALIS_EFFECT_COMPRESSOR, 4) => "Knee",
        (AURALIS_EFFECT_COMPRESSOR, 5) => "Makeup",
        (AURALIS_EFFECT_NOISE_GATE, 0) => "Threshold",
        (AURALIS_EFFECT_NOISE_GATE, 1) => "Attack",
        (AURALIS_EFFECT_NOISE_GATE, 2) => "Release",
        (AURALIS_EFFECT_NOISE_GATE, 3) => "Hold",
        (AURALIS_EFFECT_NOISE_GATE, 4) => "Range",
        (AURALIS_EFFECT_LIMITER, 0) => "Threshold",
        (AURALIS_EFFECT_LIMITER, 1) => "Release",
        (AURALIS_EFFECT_LIMITER, 2) => "Ceiling",
        (AURALIS_EFFECT_SATURATION, 0) => "Drive",
        (AURALIS_EFFECT_SATURATION, 1) => "Tone",
        (AURALIS_EFFECT_SATURATION, 2) => "Output",
        (AURALIS_EFFECT_REVERB, 0) => "Type",
        (AURALIS_EFFECT_REVERB, 1) => "Decay",
        (AURALIS_EFFECT_REVERB, 2) => "Pre-Delay",
        (AURALIS_EFFECT_REVERB, 3) => "Size",
        (AURALIS_EFFECT_REVERB, 4) => "Damping",
        (AURALIS_EFFECT_REVERB, 5) => "Width",
        (AURALIS_EFFECT_DELAY, 0) => "Time",
        (AURALIS_EFFECT_DELAY, 1) => "Feedback",
        (AURALIS_EFFECT_DELAY, 2) => "Stereo",
        (AURALIS_EFFECT_DELAY, 3) => "Tone",
        (AURALIS_EFFECT_PITCH_CORRECTION, 0) => "Key",
        (AURALIS_EFFECT_PITCH_CORRECTION, 1) => "Scale",
        (AURALIS_EFFECT_PITCH_CORRECTION, 2) => "Retune Speed",
        (AURALIS_EFFECT_PITCH_CORRECTION, 3) => "Amount",
        (AURALIS_EFFECT_PITCH_CORRECTION, 4) => "Humanize",
        (AURALIS_EFFECT_PITCH_CORRECTION, 5) => "Vibrato",
        (AURALIS_EFFECT_PITCH_CORRECTION, 6) => "Formant",
        (AURALIS_EFFECT_PITCH_CORRECTION, 7) => "Low Note",
        (AURALIS_EFFECT_PITCH_CORRECTION, 8) => "High Note",
        (AURALIS_EFFECT_PITCH_CORRECTION, 32..=43) => "Custom Note",
        _ => "Parameter",
    }
}

#[derive(Clone, Debug, Default)]
pub struct DeviceInfo {
    pub index: u32,
    pub name: String,
    pub input_channels: u32,
    pub output_channels: u32,
    pub is_default: bool,
}

#[derive(Clone, Debug, Default)]
pub struct Meters {
    pub input_peak_left: f32,
    pub input_peak_right: f32,
    pub input_rms_left: f32,
    pub input_rms_right: f32,
    pub output_peak_left: f32,
    pub output_peak_right: f32,
    pub output_rms_left: f32,
    pub output_rms_right: f32,
    pub clipping: bool,
    pub latency_ms: f64,
}

#[derive(Clone, Debug, Default)]
pub struct ParamModel {
    pub id: u16,
    pub value: f32,
    pub label: String,
    pub text: String,
}

#[derive(Clone, Debug, Default)]
pub struct EffectModel {
    pub instance_id: u32,
    pub ty: AuralisEffectType,
    pub enabled: bool,
    pub wet: f32,
    pub params: Vec<ParamModel>,
}

#[derive(Clone, Debug, Default)]
pub struct PitchModel {
    pub available: bool,
    pub detected_hz: f32,
    pub target_hz: f32,
    pub confidence: f32,
    pub shift_semitones: f32,
}

#[derive(Clone, Debug, Default)]
pub struct PresetInfo {
    pub name: String,
    pub file: String,
}

pub struct Model {
    pub engine: Option<Engine>,
    pub engine_warning: String,
    pub engine_started: bool,
    pub engine_error: String,
    pub input_devices: Vec<DeviceInfo>,
    pub output_devices: Vec<DeviceInfo>,
    pub input_error: String,
    pub output_error: String,
    pub selected_input: u32,
    pub selected_output: u32,
    pub input_gain_db: f32,
    pub output_gain_db: f32,
    pub pan: f32,
    pub muted: bool,
    pub bass_gain_db: f32,
    pub treble_gain_db: f32,
    pub reverb_mix: f32,
    pub bass_eq_instance: u32,
    pub treble_eq_instance: u32,
    pub tone_reverb_instance: u32,
    pub meters: Meters,
    pub chain: Vec<EffectModel>,
    pub pitch: PitchModel,
    pub waveform: Vec<f32>,
    pub waveform_supported: bool,
    pub test_tone: bool,
    pub test_tone_supported: bool,
    pub presets: Vec<PresetInfo>,
    pub preset_name: String,
    pub last_preset_name: String,
    pub presets_info: String,
}

impl Model {
    pub fn new() -> Self {
        let engine = Engine::load();
        let waveform_supported = engine.as_ref().map(Engine::has_waveform_api).unwrap_or(false);
        // The Windows system test sound is always available as a fallback;
        // the continuous 440 Hz tone is used when the native DLL supports it.
        let test_tone_supported = true;
        let mut engine_warning = String::new();
        if engine.is_none() {
            engine_warning = format!(
                "{} not found next to the app or on the PATH. Running in demo \
                 mode: meters read zero, controls are inert, interface is \
                 fully usable.",
                ENGINE_DLL_NAME
            );
        }
        Model {
            engine,
            engine_warning,
            engine_started: false,
            engine_error: String::new(),
            input_devices: Vec::new(),
            output_devices: Vec::new(),
            input_error: String::new(),
            output_error: String::new(),
            selected_input: 0,
            selected_output: 0,
            input_gain_db: 0.0,
            output_gain_db: 0.0,
            pan: 0.0,
            muted: false,
            bass_gain_db: 0.0,
            treble_gain_db: 0.0,
            reverb_mix: 0.0,
            bass_eq_instance: 0,
            treble_eq_instance: 0,
            tone_reverb_instance: 0,
            meters: Meters::default(),
            chain: Vec::new(),
            pitch: PitchModel::default(),
            waveform: vec![0.0; 128],
            waveform_supported,
            test_tone: false,
            test_tone_supported,
            presets: Vec::new(),
            preset_name: String::new(),
            last_preset_name: String::new(),
            presets_info: String::new(),
        }
    }

    pub fn has_engine(&self) -> bool {
        self.engine.is_some()
    }

    pub fn refresh_devices(&mut self) {
        self.input_devices.clear();
        self.output_devices.clear();
        self.input_error.clear();
        self.output_error.clear();
        if let Some(engine) = &self.engine {
            let count = engine.device_count(1);
            if count == 0 {
                self.input_error = "No input devices found.".to_string();
            }
            for index in 0..count {
                if let Some(info) = engine.device_get_info(1, index) {
                    self.input_devices.push(DeviceInfo {
                        index,
                        name: info.name(),
                        input_channels: info.input_channels,
                        output_channels: info.output_channels,
                        is_default: info.is_default != 0,
                    });
                }
            }
            let count = engine.device_count(0);
            if count == 0 {
                self.output_error = "No output devices found.".to_string();
            }
            for index in 0..count {
                if let Some(info) = engine.device_get_info(0, index) {
                    self.output_devices.push(DeviceInfo {
                        index,
                        name: info.name(),
                        input_channels: info.input_channels,
                        output_channels: info.output_channels,
                        is_default: info.is_default != 0,
                    });
                }
            }
        }
    }

    pub fn select_devices(&mut self) {
        if let Some(engine) = &self.engine {
            if !self.input_devices.is_empty() {
                engine.select_device(1, self.selected_input);
            }
            if !self.output_devices.is_empty() {
                engine.select_device(0, self.selected_output);
            }
        }
    }

    pub fn poll(&mut self) {
        if let Some(engine) = &self.engine {
            if let Ok(samples) = engine.waveform() {
                self.waveform.copy_from_slice(&samples);
            }
            if let Ok(m) = engine.meters() {
                self.meters = Meters {
                    input_peak_left: m.input_peak_left,
                    input_peak_right: m.input_peak_right,
                    input_rms_left: m.input_rms_left,
                    input_rms_right: m.input_rms_right,
                    output_peak_left: m.output_peak_left,
                    output_peak_right: m.output_peak_right,
                    output_rms_left: m.output_rms_left,
                    output_rms_right: m.output_rms_right,
                    clipping: m.clipping != 0,
                    latency_ms: m.estimated_latency_ms,
                };
            }
            if let Ok(s) = engine.mixer_state() {
                self.input_gain_db = s.input_gain_db;
                self.output_gain_db = s.output_gain_db;
                self.pan = s.pan;
                self.muted = s.muted != 0;
            }
            let chain = engine.get_chain_state();
            if let Ok(chain) = chain {
                let mut effects = Vec::new();
                for effect in chain.effects.iter().take(chain.effect_count as usize) {
                    let ty = AuralisEffectType(effect.ty);
                    let mut params = Vec::new();
                    for i in 0..effect.parameter_count as usize {
                        params.push(ParamModel {
                            id: effect.parameter_ids[i],
                            value: effect.parameter_values[i],
                            label: effect_param_label(ty, effect.parameter_ids[i])
                                .to_string(),
                            text: effect_param_text(
                                ty,
                                effect.parameter_ids[i],
                                effect.parameter_values[i],
                            ),
                        });
                    }
                    effects.push(EffectModel {
                        instance_id: effect.instance_id,
                        ty,
                        enabled: effect.enabled != 0,
                        wet: effect.wet,
                        params,
                    });
                }
                self.chain = effects;
                // Pitch telemetry (proposed C ABI addition, see API_CONTRACTS.md).
                let instance = self
                    .chain
                    .iter()
                    .find(|e| e.ty == AURALIS_EFFECT_PITCH_CORRECTION)
                    .map(|e| e.instance_id)
                    .unwrap_or(0);
                self.pitch = PitchModel {
                    available: instance != 0,
                    ..Default::default()
                };
                if instance != 0 {
                    if let Some(result) = engine.get_pitch_diagnostics(instance) {
                        if let Ok(diag) = result {
                            self.pitch = PitchModel {
                                available: true,
                                detected_hz: diag.detected_frequency_hz,
                                target_hz: diag.target_frequency_hz,
                                confidence: diag.confidence,
                                shift_semitones: diag.applied_shift_semitones,
                            };
                        }
                    }
                }
            } else {
                self.pitch = PitchModel::default();
            }
        }
    }

    pub fn start_engine(&mut self) -> i32 {
        if self.engine_started {
            if let Some(engine) = &self.engine {
                if self.test_tone { let _ = engine.set_test_tone(false, 440.0); }
                engine.stop();
            }
            self.test_tone = false;
            self.engine_started = false;
            return AURALIS_STATUS_OK;
        }
        let engine = match &self.engine {
            Some(engine) => engine,
            None => return AURALIS_STATUS_NOT_READY,
        };
        let config = AuralisAudioConfig {
            sample_rate: 48000.0,
            buffer_size: 512,
            input_channels: 2,
            output_channels: 2,
        };
        let status = engine.start(&config);
        self.engine_started = status == AURALIS_STATUS_OK;
        if self.engine_started {
            self.engine_error.clear();
        } else {
            self.engine_error = status_name(status).to_string();
        }
        status
    }

    pub fn set_input_gain(&mut self, db: f32) -> i32 {
        let status = match &self.engine {
            Some(engine) => engine.set_input_gain(db),
            None => AURALIS_STATUS_NOT_READY,
        };
        if status == AURALIS_STATUS_OK {
            self.input_gain_db = db;
        }
        status
    }

    pub fn set_output_gain(&mut self, db: f32) -> i32 {
        let status = match &self.engine {
            Some(engine) => engine.set_output_gain(db),
            None => AURALIS_STATUS_NOT_READY,
        };
        if status == AURALIS_STATUS_OK {
            self.output_gain_db = db;
        }
        status
    }

    pub fn set_pan(&mut self, pan: f32) -> i32 {
        let status = match &self.engine {
            Some(engine) => engine.set_pan(pan),
            None => AURALIS_STATUS_NOT_READY,
        };
        if status == AURALIS_STATUS_OK {
            self.pan = pan;
        }
        status
    }

    pub fn set_mute(&mut self, muted: bool) -> i32 {
        let status = match &self.engine {
            Some(engine) => engine.set_mute(muted as u32),
            None => AURALIS_STATUS_NOT_READY,
        };
        if status == AURALIS_STATUS_OK {
            self.muted = muted;
        }
        status
    }

    pub fn set_tone_control(&mut self, control: i32, value: f32) -> i32 {
        if !value.is_finite() {
            return AURALIS_STATUS_INVALID_ARGUMENT;
        }
        match control {
            0 | 1 => {
                let gain_db = value.clamp(-12.0, 12.0);
                let treble = control == 1;
                let stored_id = if treble { self.treble_eq_instance } else { self.bass_eq_instance };
                let instance_id = if stored_id != 0 {
                    stored_id
                } else {
                    let id = NEXT_EFFECT_INSTANCE.fetch_add(1, Ordering::Relaxed);
                    let Some(engine) = &self.engine else { return AURALIS_STATUS_NOT_READY };
                    let status = engine.effect_add(AURALIS_EFFECT_EQUALIZER, id, self.chain.len() as u32);
                    if status != AURALIS_STATUS_OK { return status; }
                    if treble { self.treble_eq_instance = id; } else { self.bass_eq_instance = id; }
                    id
                };
                let Some(engine) = &self.engine else { return AURALIS_STATUS_NOT_READY };
                let frequency_hz: f32 = if treble { 6_000.0 } else { 120.0 };
                let normalized_frequency = (frequency_hz / 20.0).ln() / 1_000.0_f32.ln();
                for (parameter_id, normalized) in [
                    (0, normalized_frequency),
                    (1, (0.5 + gain_db / 36.0).clamp(0.0, 1.0)),
                    (2, 0.22),
                ] {
                    let status = engine.effect_set_parameter(instance_id, parameter_id, normalized);
                    if status != AURALIS_STATUS_OK { return status; }
                }
                if treble { self.treble_gain_db = gain_db; } else { self.bass_gain_db = gain_db; }
                AURALIS_STATUS_OK
            }
            2 => {
                let mix = (value / 100.0).clamp(0.0, 1.0);
                let stored_id = self.tone_reverb_instance;
                let instance_id = if stored_id != 0 {
                    stored_id
                } else {
                    let id = NEXT_EFFECT_INSTANCE.fetch_add(1, Ordering::Relaxed);
                    let Some(engine) = &self.engine else { return AURALIS_STATUS_NOT_READY };
                    let status = engine.effect_add(AURALIS_EFFECT_REVERB, id, self.chain.len() as u32);
                    if status != AURALIS_STATUS_OK { return status; }
                    self.tone_reverb_instance = id;
                    id
                };
                let Some(engine) = &self.engine else { return AURALIS_STATUS_NOT_READY };
                let status = engine.effect_set_wet(instance_id, mix);
                if status == AURALIS_STATUS_OK { self.reverb_mix = mix; }
                status
            }
            _ => AURALIS_STATUS_INVALID_ARGUMENT,
        }
    }

    pub fn effect_set_wet(&mut self, instance_id: u32, wet: f32) -> i32 {
        let status = match &self.engine {
            Some(engine) => engine.effect_set_wet(instance_id, wet),
            None => AURALIS_STATUS_NOT_READY,
        };
        if status == AURALIS_STATUS_OK {
            if let Some(effect) = self
                .chain
                .iter_mut()
                .find(|e| e.instance_id == instance_id)
            {
                effect.wet = wet;
            }
        }
        status
    }

    pub fn effect_set_enabled(&mut self, instance_id: u32, enabled: bool) -> i32 {
        let status = match &self.engine {
            Some(engine) => engine.effect_set_enabled(instance_id, enabled as u32),
            None => AURALIS_STATUS_NOT_READY,
        };
        if status == AURALIS_STATUS_OK {
            if let Some(effect) = self
                .chain
                .iter_mut()
                .find(|e| e.instance_id == instance_id)
            {
                effect.enabled = enabled;
            }
        }
        status
    }

    pub fn effect_set_parameter(&mut self, instance_id: u32, pid: u16, value: f32) -> i32 {
        let status = match &self.engine {
            Some(engine) => engine.effect_set_parameter(instance_id, pid, value),
            None => AURALIS_STATUS_NOT_READY,
        };
        if status == AURALIS_STATUS_OK {
            if let Some(effect) = self
                .chain
                .iter_mut()
                .find(|e| e.instance_id == instance_id)
            {
                if let Some(param) = effect.params.iter_mut().find(|p| p.id == pid) {
                    param.value = value;
                    param.text = effect_param_text(effect.ty, pid, value);
                }
            }
        }
        status
    }

    pub fn toggle_effect(&mut self, ty: AuralisEffectType) -> i32 {
        let current = self
            .chain
            .iter()
            .find(|e| e.ty == ty)
            .map(|e| e.enabled)
            .unwrap_or(false);
        let instance_id = match self.chain.iter().find(|e| e.ty == ty) {
            Some(e) => e.instance_id,
            None => {
                let id = NEXT_EFFECT_INSTANCE.fetch_add(1, Ordering::Relaxed);
                let index = self.chain.len() as u32;
                let Some(engine) = &self.engine else { return AURALIS_STATUS_NOT_READY };
                let status = engine.effect_add(ty, id, index);
                if status != AURALIS_STATUS_OK { return status; }
                let _ = engine.effect_set_wet(id, 1.0);
                if ty == AURALIS_EFFECT_PITCH_CORRECTION {
                    // C major, fast retune, full correction, subtle humanize.
                    let _ = engine.effect_set_parameter(id, 0, 0.0);
                    let _ = engine.effect_set_parameter(id, 1, 1.0 / 3.0);
                    let _ = engine.effect_set_parameter(id, 2, 0.88);
                    let _ = engine.effect_set_parameter(id, 3, 1.0);
                    let _ = engine.effect_set_parameter(id, 4, 0.12);
                }
                self.chain.push(EffectModel { instance_id: id, ty, enabled: true, wet: 1.0, params: Vec::new() });
                return AURALIS_STATUS_OK;
            }
        };
        let status = match &self.engine {
            Some(engine) => engine.effect_set_enabled(instance_id, (!current) as u32),
            None => AURALIS_STATUS_NOT_READY,
        };
        if status == AURALIS_STATUS_OK {
            if let Some(effect) = self.chain.iter_mut().find(|e| e.ty == ty) {
                effect.enabled = !effect.enabled;
            }
        }
        status
    }

    pub fn toggle_test_tone(&mut self) -> i32 {
        if self.engine_started
            && self.engine.as_ref().map(Engine::has_test_tone_api).unwrap_or(false)
        {
            let enabled = !self.test_tone;
            let status = self.engine.as_ref()
                .map(|engine| engine.set_test_tone(enabled, 440.0))
                .unwrap_or(AURALIS_STATUS_NOT_READY);
            if status == AURALIS_STATUS_OK { self.test_tone = enabled; }
            status
        } else {
            play_system_test_sound()
        }
    }

    pub fn chain_summary(&self) -> String {
        if self.chain.is_empty() {
            return "Chain: empty - add effects to enable DSP".to_string();
        }
        let names: Vec<String> = self
            .chain
            .iter()
            .map(|e| format!("{} (wet {:.0}%)", e.ty.name(), (e.wet * 100.0).clamp(0.0, 100.0)))
            .collect();
        format!("Chain: {}", names.join(" -> "))
    }
}

// ---------------------------------------------------------------------------
// Presets (JSON files in the app-directory `presets/` folder)
// ---------------------------------------------------------------------------

pub fn presets_dir() -> std::path::PathBuf {
    std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(|d| d.to_path_buf()))
        .unwrap_or_else(|| std::path::PathBuf::from("."))
        .join("presets")
}

fn sanitize_name(name: &str) -> String {
    name.chars()
        .map(|c| if c.is_ascii_alphanumeric() || c == '-' || c == '_' { c } else { '_' })
        .collect()
}

impl Model {
    pub fn load_presets(&mut self) {
        self.presets.clear();
        let mut entries: Vec<(String, String)> = Vec::new();
        if let Ok(dir) = std::fs::read_dir(presets_dir()) {
            for entry in dir.flatten() {
                let path = entry.path();
                if path.extension().and_then(|e| e.to_str()) != Some("json") {
                    continue;
                }
                if let Some(stem) = path.file_stem().and_then(|s| s.to_str()) {
                    entries.push((
                        stem.to_string(),
                        entry.file_name().to_string_lossy().into_owned(),
                    ));
                }
            }
        }
        entries.sort();
        for (name, file) in entries {
            self.presets.push(PresetInfo { name, file });
        }
        self.presets_info = format!(
            "{} preset(s) in {}",
            self.presets.len(),
            presets_dir().display()
        );
    }

    fn preset_path(&self, name: &str) -> Option<std::path::PathBuf> {
        self.presets
            .iter()
            .find(|p| p.name == name)
            .map(|p| presets_dir().join(&p.file))
    }

    pub fn save_preset(&mut self, name: &str) -> std::io::Result<()> {
        let engine = match &self.engine {
            Some(e) => e,
            None => return Err(std::io::Error::other("Engine not loaded")),
        };
        let chain = engine
            .get_chain_state()
            .map_err(|s| std::io::Error::other(status_name(s)))?;
        let mut entries = Vec::new();
        for effect in chain.effects.iter().take(chain.effect_count as usize) {
            let mut parameters = Vec::new();
            for i in 0..effect.parameter_count as usize {
                parameters.push(serde_json::json!({
                    "id": effect.parameter_ids[i],
                    "value": effect.parameter_values[i],
                }));
            }
            entries.push(serde_json::json!({
                "type": effect.ty,
                "instance_id": effect.instance_id,
                "enabled": effect.enabled != 0,
                "wet": effect.wet,
                "parameters": parameters,
            }));
        }
        let doc = serde_json::json!({
            "version": 1,
            "name": name,
            "chain": entries,
        });
        let file_name = format!("{}.json", sanitize_name(name));
        let dir = presets_dir();
        std::fs::create_dir_all(&dir)?;
        std::fs::write(dir.join(file_name), serde_json::to_string_pretty(&doc)?)?;
        Ok(())
    }

    pub fn load_preset(&mut self, name: &str) -> std::io::Result<()> {
        let engine = match &self.engine {
            Some(e) => e,
            None => return Err(std::io::Error::other("Engine not loaded")),
        };
        let path = self.preset_path(name).ok_or_else(|| {
            std::io::Error::new(
                std::io::ErrorKind::NotFound,
                format!("Preset \"{name}\" not found"),
            )
        })?;
        let text = std::fs::read_to_string(path)?;
        let doc: serde_json::Value = serde_json::from_str(&text)?;

        // Remove existing chain entries.
        let existing = engine
            .get_chain_state()
            .map_err(|s| std::io::Error::other(status_name(s)))?;
        for effect in existing.effects.iter().take(existing.effect_count as usize) {
            engine.effect_remove(effect.instance_id);
        }

        let entries = doc["chain"].as_array().cloned().unwrap_or_default();
        for (index, entry) in entries.iter().enumerate() {
            let ty_value = entry["type"].as_i64().unwrap_or(-1);
            if !(0..=8).contains(&ty_value) {
                continue;
            }
            let ty = AuralisEffectType(ty_value as i32);
            let instance_id = entry["instance_id"].as_u64().unwrap_or(0) as u32;
            if instance_id == 0 {
                continue;
            }
            engine.effect_add(ty, instance_id, index as u32);
            engine.effect_set_enabled(
                instance_id,
                entry["enabled"].as_bool().unwrap_or(true) as u32,
            );
            engine.effect_set_wet(instance_id, entry["wet"].as_f64().unwrap_or(1.0) as f32);
            if let Some(parameters) = entry["parameters"].as_array() {
                for param in parameters {
                    let id = param["id"].as_u64().unwrap_or(0) as u16;
                    let value = param["value"].as_f64().unwrap_or(0.0) as f32;
                    engine.effect_set_parameter(instance_id, id, value);
                }
            }
        }
        self.bass_eq_instance = 0;
        self.treble_eq_instance = 0;
        self.tone_reverb_instance = 0;
        Ok(())
    }
}
