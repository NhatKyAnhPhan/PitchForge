#pragma once

#include <stdint.h>

#if defined(_WIN32)
    #if defined(AURALIS_BUILDING_SHARED)
        #define AURALIS_API __declspec(dllexport)
    #else
        #define AURALIS_API __declspec(dllimport)
    #endif
#elif defined(__GNUC__)
    #define AURALIS_API __attribute__((visibility("default")))
#else
    #define AURALIS_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AuralisEngine AuralisEngine;

typedef enum AuralisStatus {
    AURALIS_STATUS_OK = 0,
    AURALIS_STATUS_INVALID_ARGUMENT = 1,
    AURALIS_STATUS_DEVICE_ERROR = 2,
    AURALIS_STATUS_NOT_READY = 3,
    AURALIS_STATUS_QUEUE_FULL = 4,
    AURALIS_STATUS_INTERNAL_ERROR = 5
} AuralisStatus;

typedef struct AuralisAudioConfig {
    double sample_rate;
    uint32_t buffer_size;
    uint32_t input_channels;
    uint32_t output_channels;
} AuralisAudioConfig;

typedef struct AuralisDeviceInfo {
    uint32_t index;
    char name[192];
    uint32_t input_channels;
    uint32_t output_channels;
    uint32_t is_default;
} AuralisDeviceInfo;

typedef struct AuralisMeters {
    float input_peak_left;
    float input_peak_right;
    float input_rms_left;
    float input_rms_right;
    float output_peak_left;
    float output_peak_right;
    float output_rms_left;
    float output_rms_right;
    uint32_t clipping;
    double estimated_latency_ms;
} AuralisMeters;

typedef enum AuralisEffectType {
    AURALIS_EFFECT_NOISE_GATE = 0,
    AURALIS_EFFECT_EQUALIZER = 1,
    AURALIS_EFFECT_DE_ESSER = 2,
    AURALIS_EFFECT_COMPRESSOR = 3,
    AURALIS_EFFECT_PITCH_CORRECTION = 4,
    AURALIS_EFFECT_SATURATION = 5,
    AURALIS_EFFECT_REVERB = 6,
    AURALIS_EFFECT_DELAY = 7,
    AURALIS_EFFECT_LIMITER = 8
} AuralisEffectType;

enum {
    AURALIS_MAX_EFFECTS_PER_CHANNEL = 16,
    AURALIS_MAX_PARAMETERS_PER_EFFECT = 16
};

typedef struct AuralisEffectState {
    uint32_t instance_id;
    AuralisEffectType type;
    uint32_t enabled;
    float wet;
    uint32_t parameter_count;
    uint16_t parameter_ids[AURALIS_MAX_PARAMETERS_PER_EFFECT];
    float parameter_values[AURALIS_MAX_PARAMETERS_PER_EFFECT];
} AuralisEffectState;

typedef struct AuralisChainState {
    uint32_t effect_count;
    AuralisEffectState effects[AURALIS_MAX_EFFECTS_PER_CHANNEL];
} AuralisChainState;

AURALIS_API uint32_t auralis_api_version(void);
AURALIS_API AuralisEngine* auralis_engine_create(void);
AURALIS_API void auralis_engine_destroy(AuralisEngine* engine);
AURALIS_API AuralisStatus auralis_engine_start(
    AuralisEngine* engine,
    const AuralisAudioConfig* config);
AURALIS_API void auralis_engine_stop(AuralisEngine* engine);
AURALIS_API uint32_t auralis_engine_is_running(const AuralisEngine* engine);
AURALIS_API AuralisStatus auralis_engine_get_meters(
    const AuralisEngine* engine,
    AuralisMeters* destination);
AURALIS_API uint32_t auralis_device_count(uint32_t for_input);
AURALIS_API AuralisStatus auralis_device_get_info(
    uint32_t for_input,
    uint32_t index,
    AuralisDeviceInfo* destination);
AURALIS_API AuralisStatus auralis_engine_select_device(
    AuralisEngine* engine,
    uint32_t for_input,
    uint32_t index);
AURALIS_API AuralisStatus auralis_engine_effect_add(
    AuralisEngine* engine,
    AuralisEffectType type,
    uint32_t instance_id,
    uint32_t index);
AURALIS_API AuralisStatus auralis_engine_effect_remove(
    AuralisEngine* engine,
    uint32_t instance_id);
AURALIS_API AuralisStatus auralis_engine_effect_move(
    AuralisEngine* engine,
    uint32_t instance_id,
    uint32_t destination_index);
AURALIS_API AuralisStatus auralis_engine_effect_set_enabled(
    AuralisEngine* engine,
    uint32_t instance_id,
    uint32_t enabled);
AURALIS_API AuralisStatus auralis_engine_effect_set_parameter(
    AuralisEngine* engine,
    uint32_t instance_id,
    uint16_t parameter_id,
    float normalized_value);
AURALIS_API AuralisStatus auralis_engine_get_chain_state(
    const AuralisEngine* engine,
    AuralisChainState* destination);

#ifdef __cplusplus
}
#endif
