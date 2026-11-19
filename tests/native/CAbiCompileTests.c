#include <auralis/bridge/auralis_c_api.h>

int main(void) {
    AuralisAudioConfig config = {48000.0, 256, 1, 2};
    AuralisChainState chain = {0};
    AuralisEffectState effect = {0};
    AuralisMixerState mixer = {0};
    effect.type = AURALIS_EFFECT_EQUALIZER;
    chain.effects[0] = effect;
    return config.input_channels == 1 && chain.effect_count == 0
        && mixer.muted == 0 ? 0 : 1;
}
