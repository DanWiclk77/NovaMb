#pragma once
#include <juce_dsp/juce_dsp.h>

namespace NovaMB {

enum class Mode { Compress, Expand };
enum class SidechainSource { Internal, External };

struct BandParameters {
    float frequencyLow  = 20.0f;
    float frequencyHigh = 20000.0f;
    float threshold     = -20.0f;
    float ratio         = 4.0f;
    float attack        = 20.0f;
    float release       = 100.0f;
    float knee          = 6.0f;
    float makeUpGain    = 0.0f;
    float lookahead     = 0.0f;
    Mode mode                     = Mode::Compress;
    SidechainSource sidechainSource = SidechainSource::Internal;
    bool solo     = false;
    bool mute     = false;
    bool bypassed = false;
    bool active   = true;
};

// ---------------------------------------------------------------------------
// SimpleExpander — implementación propia porque juce::dsp::Expander no existe
// ---------------------------------------------------------------------------
struct SimpleExpander {
    void prepare(const juce::dsp::ProcessSpec& spec) {
        sampleRate = (float)spec.sampleRate;
        envelope   = 0.0f;
        updateCoefficients();
    }

    void setThreshold (float dB)  { thresholdDb = dB; }
    void setRatio     (float r)   { ratio = juce::jmax(1.0f, r); }
    void setAttack    (float ms)  { attackMs  = ms;  updateCoefficients(); }
    void setRelease   (float ms)  { releaseMs = ms;  updateCoefficients(); }

    // Procesa un bloque completo in-place
    void process(juce::dsp::ProcessContextReplacing<float>& ctx) {
        auto& block = ctx.getOutputBlock();
        for (size_t s = 0; s < block.getNumSamples(); ++s) {
            // Calcular nivel de entrada (promedio de canales)
            float sum = 0.0f;
            for (size_t ch = 0; ch < block.getNumChannels(); ++ch)
                sum += std::abs(block.getChannelPointer(ch)[s]);
            float inputLevel = sum / (float)block.getNumChannels();

            float inputDb = juce::Decibels::gainToDecibels(inputLevel + 1e-9f);

            // Detector de envolvente (peak)
            float coef = (inputDb > envelope) ? attackCoef : releaseCoef;
            envelope = coef * envelope + (1.0f - coef) * inputDb;

            // Calcular ganancia de expansión
            float gainDb = 0.0f;
            if (envelope < thresholdDb)
                gainDb = (thresholdDb - envelope) * (1.0f / ratio - 1.0f);  // negativo → atenuar

            float gainLinear = juce::Decibels::decibelsToGain(gainDb);

            // Aplicar a todos los canales
            for (size_t ch = 0; ch < block.getNumChannels(); ++ch)
                block.getChannelPointer(ch)[s] *= gainLinear;
        }
    }

    // Devuelve reducción de ganancia actual en dB (negativo = atenuación)
    float getGainReductionDb() const { return lastGainDb; }

private:
    void updateCoefficients() {
        if (sampleRate <= 0.0f) return;
        attackCoef  = std::exp(-1.0f / (sampleRate * attackMs  * 0.001f));
        releaseCoef = std::exp(-1.0f / (sampleRate * releaseMs * 0.001f));
    }

    float sampleRate  = 44100.0f;
    float thresholdDb = -40.0f;
    float ratio       = 2.0f;
    float attackMs    = 10.0f;
    float releaseMs   = 100.0f;
    float attackCoef  = 0.0f;
    float releaseCoef = 0.0f;
    float envelope    = 0.0f;
    float lastGainDb  = 0.0f;
};

// ---------------------------------------------------------------------------

class CompressorBand {
public:
    CompressorBand();
    void  prepare(const juce::dsp::ProcessSpec& spec);
    void  process(juce::dsp::ProcessContextReplacing<float>& context,
                  const juce::AudioBuffer<float>& sidechainBuffer);
    void  updateParameters(const BandParameters& params);
    float getGainReduction() const;
    bool  isSolo()   const { return parameters.solo; }
    bool  isMute()   const { return parameters.mute; }
    bool  isActive() const { return parameters.active; }

private:
    juce::dsp::LinkwitzRileyFilter<float> loPass, hiPass;
    juce::dsp::Compressor<float>          compressor;
    SimpleExpander                        expander;   // ← reemplazo correcto
    juce::dsp::Gain<float>                gain;
    BandParameters parameters;
    float lastReduction = 0.0f;
};

// ---------------------------------------------------------------------------

class MultibandEngine {
public:
    MultibandEngine();
    void  prepare(const juce::dsp::ProcessSpec& spec);
    void  process(juce::AudioBuffer<float>& buffer,
                  const juce::AudioBuffer<float>& sidechainBuffer);
    void  updateBand(int index, const BandParameters& params);
    float getGainReduction(int index) const;

    // FFT para visualización
    void getFFTData         (float* dest);
    void getSidechainFFTData(float* dest);
    int  getFFTSize() const { return 1 << fftOrder; }

private:
    void pushNextSampleIntoFifo(float sample, float* fifo, int& index, bool& ready);

    std::vector<std::unique_ptr<CompressorBand>> bands;
    juce::dsp::ProcessSpec currentSpec;

    static constexpr int fftOrder = 11;
    juce::dsp::FFT                       fft;
    juce::dsp::WindowingFunction<float>  window;

    float fifo   [1 << fftOrder]        = {};
    float fftData[1 << (fftOrder + 1)]  = {};
    int   fifoIndex          = 0;
    bool  nextFFTBlockReady  = false;
    float scopeData[1 << fftOrder]      = {};

    float scFifo   [1 << fftOrder]      = {};
    float scFftData[1 << (fftOrder + 1)]= {};
    int   scFifoIndex        = 0;
    bool  scNextFFTBlockReady = false;
    float scScopeData[1 << fftOrder]    = {};
};

} // namespace NovaMB
