//! PitchForge product UI — AURALIS STUDIO (Rust/Slint).
//!
//! Binds to the PitchForge native engine through the C ABI at runtime
//! (`auralis_audio_engine.dll`). UI updates flow through a poll loop that
//! calls `Model::poll` on the Slint event loop; slider changes flow back to
//! the engine via two-way bindings (App props -> `sync_ui`).

slint::include_modules!();

mod ffi;
mod model;

use std::sync::{Arc, Mutex};
use std::time::Duration;

use ffi::{
    AURALIS_EFFECT_COMPRESSOR, AURALIS_EFFECT_DE_ESSER, AURALIS_EFFECT_DELAY,
    AURALIS_EFFECT_EQUALIZER, AURALIS_EFFECT_LIMITER, AURALIS_EFFECT_NOISE_GATE,
    AURALIS_EFFECT_PITCH_CORRECTION, AURALIS_EFFECT_REVERB, AURALIS_EFFECT_SATURATION,
};

use model::*;

fn ss(text: String) -> slint::SharedString {
    slint::SharedString::from(text)
}

fn main() -> Result<(), slint::PlatformError> {
    let model = Arc::new(Mutex::new(Model::new()));
    {
        let mut m = model.lock().unwrap();
        m.refresh_devices();
        m.load_presets();
    }

    let app = App::new().expect("create App");
    let app_weak = app.as_weak();
    {
        let model = model.lock().unwrap();
        push_state(&app, &model);
    }

    // ------------------------------------------------------------------
    // App callbacks (Slint -> Rust).
    // ------------------------------------------------------------------
    {
        let model = model.clone();
        app.on_mute_toggle(move || {
            let mut model = model.lock().unwrap();
            let muted = !model.muted;
            model.set_mute(muted);
        });
    }
    {
        let model = model.clone();
        let app_weak = app_weak.clone();
        app.on_refresh_devices(move || {
            let Some(app) = app_weak.upgrade() else { return };
            let mut model = model.lock().unwrap();
            model.refresh_devices();
            model.select_devices();
            push_state(&app, &model);
        });
    }
    {
        let model = model.clone();
        let app_weak = app_weak.clone();
        app.on_tone_change(move |control, value| {
            let Some(app) = app_weak.upgrade() else { return };
            let mut model = model.lock().unwrap();
            let status = model.set_tone_control(control, value);
            if status != ffi::AURALIS_STATUS_OK {
                model.engine_error = status_name(status).to_string();
            } else {
                model.engine_error.clear();
            }
            push_state(&app, &model);
        });
    }
    {
        let model = model.clone();
        app.on_start_engine(move || {
            let mut model = model.lock().unwrap();
            model.start_engine();
        });
    }
    {
        let model = model.clone();
        let app_weak = app_weak.clone();
        app.on_save_preset(move || {
            let Some(app) = app_weak.upgrade() else { return };
            let mut model = model.lock().unwrap();
            save_preset(&app, &mut model);
        });
    }
    {
        let model = model.clone();
        let app_weak = app_weak.clone();
        app.on_load_preset(move || {
            let Some(app) = app_weak.upgrade() else { return };
            let mut model = model.lock().unwrap();
            load_preset(&app, &mut model);
        });
    }
    {
        let model = model.clone();
        app.on_ng_toggle(move || {
            let mut model = model.lock().unwrap();
            model.toggle_effect(AURALIS_EFFECT_NOISE_GATE);
        });
    }
    {
        let model = model.clone();
        app.on_eq_toggle(move || {
            let mut model = model.lock().unwrap();
            model.toggle_effect(AURALIS_EFFECT_EQUALIZER);
        });
    }
    {
        let model = model.clone();
        app.on_deesser_toggle(move || {
            let mut model = model.lock().unwrap();
            model.toggle_effect(AURALIS_EFFECT_DE_ESSER);
        });
    }
    {
        let model = model.clone();
        app.on_comp_toggle(move || {
            let mut model = model.lock().unwrap();
            model.toggle_effect(AURALIS_EFFECT_COMPRESSOR);
        });
    }
    {
        let model = model.clone();
        app.on_pitch_toggle(move || {
            let mut model = model.lock().unwrap();
            model.toggle_effect(AURALIS_EFFECT_PITCH_CORRECTION);
        });
    }
    {
        let model = model.clone();
        app.on_sat_toggle(move || {
            let mut model = model.lock().unwrap();
            model.toggle_effect(AURALIS_EFFECT_SATURATION);
        });
    }
    {
        let model = model.clone();
        app.on_rev_toggle(move || {
            let mut model = model.lock().unwrap();
            model.toggle_effect(AURALIS_EFFECT_REVERB);
        });
    }
    {
        let model = model.clone();
        app.on_delay_toggle(move || {
            let mut model = model.lock().unwrap();
            model.toggle_effect(AURALIS_EFFECT_DELAY);
        });
    }
    {
        let model = model.clone();
        app.on_lim_toggle(move || {
            let mut model = model.lock().unwrap();
            model.toggle_effect(AURALIS_EFFECT_LIMITER);
        });
    }
    {
        let model = model.clone();
        app.on_test_tone_toggle(move || {
            let mut model = model.lock().unwrap();
            let status = model.toggle_test_tone();
            if status != ffi::AURALIS_STATUS_OK {
                model.engine_error = if !model.engine_started {
                    "Start the audio engine before playing a test tone".into()
                } else {
                    format!("Test tone unavailable: {}", status_name(status))
                };
            } else {
                model.engine_error.clear();
            }
        });
    }

    // ------------------------------------------------------------------
    // Poll thread — pushes state to Slint every ~40ms
    // ------------------------------------------------------------------
    let poll_model = model.clone();
    let poll_app = app_weak.clone();
    std::thread::spawn(move || loop {
        std::thread::sleep(Duration::from_millis(40));
        let app_w = poll_app.clone();
        let model_w = poll_model.clone();
        let _ = slint::invoke_from_event_loop(move || {
            let Some(app) = app_w.upgrade() else { return };
            let mut model = model_w.lock().unwrap();
            sync_ui(&app, &mut model);
            model.poll();
            push_state(&app, &model);
        });
    });

    app.run()
}

fn save_preset(app: &App, model: &mut Model) {
    let name = app.get_preset_name();
    let mut name = name.to_string();
    if name.trim().is_empty() {
        name = format!("Preset {}", model.presets.len() + 1);
    }
    match model.save_preset(&name) {
        Ok(()) => {
            model.load_presets();
            app.set_presets_info(ss(format!("Saved preset \"{name}\"")));
        }
        Err(e) => app.set_presets_info(ss(format!("Save failed: {e}"))),
    }
    app.set_preset_name("".into());
}

fn load_preset(app: &App, model: &mut Model) {
    let sel = app.get_selected_preset().max(0) as usize;
    if let Some(preset) = model.presets.get(sel) {
        let name = preset.name.clone();
        match model.load_preset(&name) {
            Ok(()) => app.set_presets_info(ss(format!("Loaded preset \"{name}\""))),
            Err(e) => app.set_presets_info(ss(format!("Load failed: {e}"))),
        }
    }
}

// ---------------------------------------------------------------------------
// Poll-side helpers (run on the Slint event loop via invoke_from_event_loop)
// ---------------------------------------------------------------------------

fn sync_ui(app: &App, model: &mut Model) {
    // Devices (two-way binding via ComboBox current-index).
    if app.get_selected_input().max(0) as u32 != model.selected_input {
        model.selected_input = app.get_selected_input().max(0) as u32;
        model.select_devices();
    }
    if app.get_selected_output().max(0) as u32 != model.selected_output {
        model.selected_output = app.get_selected_output().max(0) as u32;
        model.select_devices();
    }

    // Mixer faders (two-way). Model always takes the slider value; the engine
    // accepts only while running, so dragging while stopped still sticks.
    model.input_gain_db = app.get_input_gain_db();
    model.set_input_gain(model.input_gain_db);
    model.output_gain_db = app.get_output_gain_db();
    model.set_output_gain(model.output_gain_db);
    model.pan = app.get_pan();
    model.set_pan(model.pan);
    if app.get_muted() != model.muted {
        model.muted = app.get_muted();
        model.set_mute(model.muted);
    }

    // Presets combo (selection change -> load preset).
    let sel = app.get_selected_preset().max(0) as usize;
    if let Some(preset) = model.presets.get(sel) {
        let name = preset.name.clone();
        if name != model.last_preset_name {
            model.last_preset_name = name.clone();
            model.load_preset(&name);
            app.set_presets_info(ss(format!("Loaded preset \"{name}\"")));
        }
    }
}

fn push_state(app: &App, model: &Model) {
    push_state_inner(app, model);
}

fn push_state_inner(app: &App, model: &Model) {
    let names = |list: &[String]| -> slint::ModelRc<slint::SharedString> {
        slint::ModelRc::new(slint::VecModel::from_iter(
            list.iter().map(|s| slint::SharedString::from(s.clone())),
        ))
    };

    let input_names: Vec<String> = model.input_devices.iter().map(|d| d.name.clone()).collect();
    let output_names: Vec<String> = model.output_devices.iter().map(|d| d.name.clone()).collect();
    app.set_input_device_names(names(&input_names));
    app.set_output_device_names(names(&output_names));
    app.set_selected_input(model.selected_input as i32);
    app.set_selected_output(model.selected_output as i32);
    app.set_input_device_status(ss(
        model
            .input_devices
            .get(model.selected_input as usize)
            .map(|d| format!("{} ({} ch in, {} ch out)", d.name, d.input_channels, d.output_channels))
            .unwrap_or_else(|| "No input device selected".into()),
    ));
    app.set_output_device_status(ss(
        model
            .output_devices
            .get(model.selected_output as usize)
            .map(|d| format!("{} ({} ch in, {} ch out)", d.name, d.input_channels, d.output_channels))
            .unwrap_or_else(|| "No output device selected".into()),
    ));

    app.set_engine_warning(ss(model.engine_warning.clone()));
    app.set_engine_started(model.engine_started);
    app.set_engine_error(ss(model.engine_error.clone()));
    app.set_start_button_text(ss(if model.engine_started {
        "Stop Engine".into()
    } else {
        "Start Engine".into()
    }));

    app.set_input_gain_db(model.input_gain_db);
    app.set_output_gain_db(model.output_gain_db);
    app.set_pan(model.pan);
    app.set_muted(model.muted);
    app.set_input_gain_text(ss(db_text(model.input_gain_db)));
    app.set_output_gain_text(ss(db_text(model.output_gain_db)));
    app.set_pan_text(ss(format!("{:+.2}", model.pan)));
    app.set_bass_gain_db(model.bass_gain_db);
    app.set_treble_gain_db(model.treble_gain_db);
    app.set_reverb_mix(model.reverb_mix);
    app.set_bass_gain_text(ss(format!("{:+.1} dB", model.bass_gain_db)));
    app.set_treble_gain_text(ss(format!("{:+.1} dB", model.treble_gain_db)));
    app.set_reverb_mix_text(ss(format!("{:.0}%", model.reverb_mix * 100.0)));

    app.set_in_peak_l(model.meters.input_peak_left);
    app.set_in_peak_r(model.meters.input_peak_right);
    app.set_in_rms_l(model.meters.input_rms_left);
    app.set_in_rms_r(model.meters.input_rms_right);
    app.set_out_peak_l(model.meters.output_peak_left);
    app.set_out_peak_r(model.meters.output_peak_right);
    app.set_out_rms_l(model.meters.output_rms_left);
    app.set_out_rms_r(model.meters.output_rms_right);
    app.set_clipping(model.meters.clipping);
    app.set_latency_text(ss(latency_text(model.meters.latency_ms)));

    push_effects(app, model);

    app.set_pitch_available(model.pitch.available);
    app.set_pitch_detected_hz(model.pitch.detected_hz);
    app.set_pitch_target_hz(model.pitch.target_hz);
    app.set_pitch_confidence(model.pitch.confidence);
    app.set_pitch_shift(model.pitch.shift_semitones);
    app.set_pitch_detected_text(ss(if model.pitch.available {
        hz_text(model.pitch.detected_hz)
    } else {
        String::new()
    }));
    app.set_pitch_target_text(ss(if model.pitch.available {
        hz_text(model.pitch.target_hz)
    } else {
        String::new()
    }));
    app.set_pitch_confidence_text(ss(format!(
        "{:.0}%",
        (model.pitch.confidence * 100.0).clamp(0.0, 100.0)
    )));
    app.set_pitch_shift_text(ss(format!("{:+.1} st", model.pitch.shift_semitones)));

    let preset_names: Vec<String> = model.presets.iter().map(|p| p.name.clone()).collect();
    app.set_preset_names(names(&preset_names));
    app.set_selected_preset(
        model
            .presets
            .iter()
            .position(|p| p.name == model.last_preset_name)
            .unwrap_or(0) as i32,
    );
    app.set_preset_name(ss(model.preset_name.clone()));
    app.set_presets_info(ss(model.presets_info.clone()));

    app.set_chain_summary(ss(model.chain_summary()));
    app.set_waveform(slint::ModelRc::new(slint::VecModel::from_iter(
        model.waveform.iter().copied(),
    )));
    app.set_waveform_supported(model.waveform_supported);
    app.set_test_tone(model.test_tone);
    app.set_test_tone_supported(model.test_tone_supported);
}

fn param_at(e: Option<&EffectModel>, i: usize) -> (bool, f32, slint::SharedString) {
    e.and_then(|e| e.params.iter().find(|p| p.id == i as u16))
        .map(|p| (true, p.value, slint::SharedString::from(p.text.clone())))
        .unwrap_or((false, 0.0, slint::SharedString::from("")))
}

fn push_effects(app: &App, model: &Model) {
    // One block per effect type: present / enabled / wet / wet_text + params.
    let e = model.chain.iter().find(|e| e.ty == AURALIS_EFFECT_NOISE_GATE);
    app.set_ng_present(e.is_some());
    app.set_ng_enabled(e.map(|e| e.enabled).unwrap_or(false));
    app.set_ng_wet(e.map(|e| e.wet).unwrap_or(0.0));
    app.set_ng_wet_text(ss(e.map(|e| format!("{:.2}", e.wet)).unwrap_or_else(|| "0.00".into())));
    for i in 0..5 {
        let (present, value, text) = param_at(e, i);
        match i {
            0 => { app.set_ng_p0(present); app.set_ng_p0v(value); app.set_ng_p0t(text); }
            1 => { app.set_ng_p1(present); app.set_ng_p1v(value); app.set_ng_p1t(text); }
            2 => { app.set_ng_p2(present); app.set_ng_p2v(value); app.set_ng_p2t(text); }
            3 => { app.set_ng_p3(present); app.set_ng_p3v(value); app.set_ng_p3t(text); }
            4 => { app.set_ng_p4(present); app.set_ng_p4v(value); app.set_ng_p4t(text); }
            _ => {}
        }
    }

    let e = model.chain.iter().find(|e| e.ty == AURALIS_EFFECT_EQUALIZER);
    app.set_eq_present(e.is_some());
    app.set_eq_enabled(e.map(|e| e.enabled).unwrap_or(false));
    app.set_eq_wet(e.map(|e| e.wet).unwrap_or(0.0));
    app.set_eq_wet_text(ss(e.map(|e| format!("{:.2}", e.wet)).unwrap_or_else(|| "0.00".into())));
    for i in 0..6 {
        let (present, value, text) = param_at(e, i);
        match i {
            0 => { app.set_eq_p0(present); app.set_eq_p0v(value); app.set_eq_p0t(text); }
            1 => { app.set_eq_p1(present); app.set_eq_p1v(value); app.set_eq_p1t(text); }
            2 => { app.set_eq_p2(present); app.set_eq_p2v(value); app.set_eq_p2t(text); }
            3 => { app.set_eq_p3(present); app.set_eq_p3v(value); app.set_eq_p3t(text); }
            4 => { app.set_eq_p4(present); app.set_eq_p4v(value); app.set_eq_p4t(text); }
            5 => { app.set_eq_p5(present); app.set_eq_p5v(value); app.set_eq_p5t(text); }
            _ => {}
        }
    }

    let e = model.chain.iter().find(|e| e.ty == AURALIS_EFFECT_DE_ESSER);
    app.set_deesser_present(e.is_some());
    app.set_deesser_enabled(e.map(|e| e.enabled).unwrap_or(false));
    app.set_deesser_wet(e.map(|e| e.wet).unwrap_or(0.0));
    app.set_deesser_wet_text(ss(e.map(|e| format!("{:.2}", e.wet)).unwrap_or_else(|| "0.00".into())));
    for i in 0..5 {
        let (present, value, text) = param_at(e, i);
        match i {
            0 => { app.set_deesser_p0(present); app.set_deesser_p0v(value); app.set_deesser_p0t(text); }
            1 => { app.set_deesser_p1(present); app.set_deesser_p1v(value); app.set_deesser_p1t(text); }
            2 => { app.set_deesser_p2(present); app.set_deesser_p2v(value); app.set_deesser_p2t(text); }
            3 => { app.set_deesser_p3(present); app.set_deesser_p3v(value); app.set_deesser_p3t(text); }
            4 => { app.set_deesser_p4(present); app.set_deesser_p4v(value); app.set_deesser_p4t(text); }
            _ => {}
        }
    }

    let e = model.chain.iter().find(|e| e.ty == AURALIS_EFFECT_COMPRESSOR);
    app.set_comp_present(e.is_some());
    app.set_comp_enabled(e.map(|e| e.enabled).unwrap_or(false));
    app.set_comp_wet(e.map(|e| e.wet).unwrap_or(0.0));
    app.set_comp_wet_text(ss(e.map(|e| format!("{:.2}", e.wet)).unwrap_or_else(|| "0.00".into())));
    for i in 0..6 {
        let (present, value, text) = param_at(e, i);
        match i {
            0 => { app.set_comp_p0(present); app.set_comp_p0v(value); app.set_comp_p0t(text); }
            1 => { app.set_comp_p1(present); app.set_comp_p1v(value); app.set_comp_p1t(text); }
            2 => { app.set_comp_p2(present); app.set_comp_p2v(value); app.set_comp_p2t(text); }
            3 => { app.set_comp_p3(present); app.set_comp_p3v(value); app.set_comp_p3t(text); }
            4 => { app.set_comp_p4(present); app.set_comp_p4v(value); app.set_comp_p4t(text); }
            5 => { app.set_comp_p5(present); app.set_comp_p5v(value); app.set_comp_p5t(text); }
            _ => {}
        }
    }

    let e = model.chain.iter().find(|e| e.ty == AURALIS_EFFECT_PITCH_CORRECTION);
    app.set_pitch_present(e.is_some());
    app.set_pitch_enabled(e.map(|e| e.enabled).unwrap_or(false));
    app.set_pitch_wet(e.map(|e| e.wet).unwrap_or(0.0));
    app.set_pitch_wet_text(ss(e.map(|e| format!("{:.2}", e.wet)).unwrap_or_else(|| "0.00".into())));
    for i in 0..6 {
        let (present, value, text) = param_at(e, i);
        match i {
            0 => { app.set_pitch_p0(present); app.set_pitch_p0v(value); app.set_pitch_p0t(text); }
            1 => { app.set_pitch_p1(present); app.set_pitch_p1v(value); app.set_pitch_p1t(text); }
            2 => { app.set_pitch_p2(present); app.set_pitch_p2v(value); app.set_pitch_p2t(text); }
            3 => { app.set_pitch_p3(present); app.set_pitch_p3v(value); app.set_pitch_p3t(text); }
            4 => { app.set_pitch_p4(present); app.set_pitch_p4v(value); app.set_pitch_p4t(text); }
            5 => { app.set_pitch_p5(present); app.set_pitch_p5v(value); app.set_pitch_p5t(text); }
            _ => {}
        }
    }

    let e = model.chain.iter().find(|e| e.ty == AURALIS_EFFECT_SATURATION);
    app.set_sat_present(e.is_some());
    app.set_sat_enabled(e.map(|e| e.enabled).unwrap_or(false));
    app.set_sat_wet(e.map(|e| e.wet).unwrap_or(0.0));
    app.set_sat_wet_text(ss(e.map(|e| format!("{:.2}", e.wet)).unwrap_or_else(|| "0.00".into())));
    for i in 0..3 {
        let (present, value, text) = param_at(e, i);
        match i {
            0 => { app.set_sat_p0(present); app.set_sat_p0v(value); app.set_sat_p0t(text); }
            1 => { app.set_sat_p1(present); app.set_sat_p1v(value); app.set_sat_p1t(text); }
            2 => { app.set_sat_p2(present); app.set_sat_p2v(value); app.set_sat_p2t(text); }
            _ => {}
        }
    }

    let e = model.chain.iter().find(|e| e.ty == AURALIS_EFFECT_REVERB);
    app.set_rev_present(e.is_some());
    app.set_rev_enabled(e.map(|e| e.enabled).unwrap_or(false));
    app.set_rev_wet(e.map(|e| e.wet).unwrap_or(0.0));
    app.set_rev_wet_text(ss(e.map(|e| format!("{:.2}", e.wet)).unwrap_or_else(|| "0.00".into())));
    for i in 0..6 {
        let (present, value, text) = param_at(e, i);
        match i {
            0 => { app.set_rev_p0(present); app.set_rev_p0v(value); app.set_rev_p0t(text); }
            1 => { app.set_rev_p1(present); app.set_rev_p1v(value); app.set_rev_p1t(text); }
            2 => { app.set_rev_p2(present); app.set_rev_p2v(value); app.set_rev_p2t(text); }
            3 => { app.set_rev_p3(present); app.set_rev_p3v(value); app.set_rev_p3t(text); }
            4 => { app.set_rev_p4(present); app.set_rev_p4v(value); app.set_rev_p4t(text); }
            5 => { app.set_rev_p5(present); app.set_rev_p5v(value); app.set_rev_p5t(text); }
            _ => {}
        }
    }

    let e = model.chain.iter().find(|e| e.ty == AURALIS_EFFECT_DELAY);
    app.set_delay_present(e.is_some());
    app.set_delay_enabled(e.map(|e| e.enabled).unwrap_or(false));
    app.set_delay_wet(e.map(|e| e.wet).unwrap_or(0.0));
    app.set_delay_wet_text(ss(e.map(|e| format!("{:.2}", e.wet)).unwrap_or_else(|| "0.00".into())));
    for i in 0..4 {
        let (present, value, text) = param_at(e, i);
        match i {
            0 => { app.set_delay_p0(present); app.set_delay_p0v(value); app.set_delay_p0t(text); }
            1 => { app.set_delay_p1(present); app.set_delay_p1v(value); app.set_delay_p1t(text); }
            2 => { app.set_delay_p2(present); app.set_delay_p2v(value); app.set_delay_p2t(text); }
            3 => { app.set_delay_p3(present); app.set_delay_p3v(value); app.set_delay_p3t(text); }
            _ => {}
        }
    }

    let e = model.chain.iter().find(|e| e.ty == AURALIS_EFFECT_LIMITER);
    app.set_lim_present(e.is_some());
    app.set_lim_enabled(e.map(|e| e.enabled).unwrap_or(false));
    app.set_lim_wet(e.map(|e| e.wet).unwrap_or(0.0));
    app.set_lim_wet_text(ss(e.map(|e| format!("{:.2}", e.wet)).unwrap_or_else(|| "0.00".into())));
    for i in 0..3 {
        let (present, value, text) = param_at(e, i);
        match i {
            0 => { app.set_lim_p0(present); app.set_lim_p0v(value); app.set_lim_p0t(text); }
            1 => { app.set_lim_p1(present); app.set_lim_p1v(value); app.set_lim_p1t(text); }
            2 => { app.set_lim_p2(present); app.set_lim_p2v(value); app.set_lim_p2t(text); }
            _ => {}
        }
    }
}
