≠rá^—f•ñÿ¶{}¨y 'v√Æ∂õ≠#include "TextureSynthesizer.h"

#include "RenderQuality.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace
{
constexpr int spectrumBands = 8;
constexpr int maximumAnalysisFrames = 256;
constexpr int fingerprintBands = 96;

struct BoundaryFeature
{
    std::array<float, spectrumBands> spectrum {};
    float rms = 0.0f;
    float derivative = 0.0f;
    float stereoCorrelation = 1.0f;
};

class DeterministicRandom
{
public:
    explicit DeterministicRandom(const uint32_t initial)
        : state(initial != 0 ? initial : 1u)
    {
    }

    uint32_t next() noexcept
    {
        auto value = state;
        value ^= value << 13u;
        value ^= value >> 17u;
        value ^= value << 5u;
        state = value;
        return value;
    }

    float unit() noexcept
    {
        return static_cast<float>(next() & 0x00ffffffu)
               / static_cast<float>(0x01000000u);
    }

private:
    uint32_t state;
};

float readMono(const juce::AudioBuffer<float>& audio, const int sample)
{
    auto value = 0.0f;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        value += audio.getSample(channel, sample);
    return value / static_cast<float>(juce::jmax(1, audio.getNumChannels()));
}

float readComponent(const juce::AudioBuffer<float>& audio,
                    const int component,
                    const int sample)
{
    if (audio.getNumChannels() < 2)
        return audio.getSample(0, sample);
    constexpr auto inverseRootTwo = 0.7071067811865475f;
    const auto left = audio.getSample(0, sample);
    const auto right = audio.getSample(1, sample);
    return component == 0 ? (left + right) * inverseRootTwo
                          : (left - right) * inverseRootTwo;
}

BoundaryFeature analyseBoundary(const juce::AudioBuffer<float>& source,
                                const int firstSample,
                                const int sampleCount)
{
    BoundaryFeature feature;
    const auto count = juce::jlimit(
        16, source.getNumSamples(), juce::jmax(16, sampleCount));
    const auto start = juce::jlimit(
        0, juce::jmax(0, source.getNumSamples() - count), firstSample);
    const auto stride = juce::jmax(1, count / 256);
    auto energy = 0.0;
    auto differenceEnergy = 0.0;
    auto stereoDot = 0.0;
    auto stereoLeft = 0.0;
    auto stereoRight = 0.0;
    auto previous = readMono(source, start);
    int analysed = 0;

    for (int offset = 0; offset < count; offset += stride)
    {
        const auto position = juce::jmin(source.getNumSamples() - 1, start + offset);
        const auto value = readMono(source, position);
        energy += static_cast<double>(value) * value;
        const auto difference = value - previous;
        differenceEnergy += static_cast<double>(difference) * difference;
        previous = value;
        if (source.getNumChannels() > 1)
        {
            const auto left = source.getSample(0, position);
            const auto right = source.getSample(1, position);
            stereoDot += static_cast<double>(left) * right;
            stereoLeft += static_cast<double>(left) * left;
            stereoRight += static_cast<double>(right) * right;
        }
        ++analysed;
    }

    feature.rms = std::sqrt(
        static_cast<float>(energy / static_cast<double>(juce::jmax(1, analysed))));
    feature.derivative = std::sqrt(
        static_cast<float>(differenceEnergy
                           / static_cast<double>(juce::jmax(1, analysed))));
    if (source.getNumChannels() > 1)
        feature.stereoCorrelation = static_cast<float>(
            stereoDot / std::sqrt(juce::jmax(1.0e-12, stereoLeft * stereoRight)));

    constexpr int transformSize = 128;
    std::array<float, transformSize> window {};
    for (int index = 0; index < transformSize; ++index)
    {
        const auto sourceOffset = index * juce::jmax(1, count - 1)
                                  / (transformSize - 1);
        const auto position = juce::jmin(source.getNumSamples() - 1,
                                         start + sourceOffset);
        const auto hann = 0.5f - 0.5f * std::cos(
            juce::MathConstants<float>::twoPi * static_cast<float>(index)
            / static_cast<float>(transformSize - 1));
        window[static_cast<size_t>(index)] = readMono(source, position) * hann;
    }
    for (int band = 0; band < spectrumBands; ++band)
    {
        const auto bin = 1 + band * 3;
        auto real = 0.0;
        auto imaginary = 0.0;
        for (int index = 0; index < transformSize; ++index)
        {
            const auto phase = juce::MathConstants<double>::twoPi
                               * static_cast<double>(bin * index)
                               / static_cast<double>(transformSize);
            real += window[static_cast<size_t>(index)] * std::cos(phase);
            imaginary -= window[static_cast<size_t>(index)] * std::sin(phase);
        }
        feature.spectrum[static_cast<size_t>(band)] = std::log1p(
            static_cast<float>(std::sqrt(real * real + imaginary * imaginary)));
    }
    const auto spectrumTotal = std::accumulate(
        feature.spectrum.begin(), feature.spectrum.end(), 1.0e-6f);
    for (auto& value : feature.spectrum)
        value /= spectrumTotal;
    return feature;
}

float spectrumDistance(const BoundaryFeature& left, const BoundaryFeature& right)
{
    auto distance = 0.0f;
    for (int band = 0; band < spectrumBands; ++band)
        distance += std::abs(left.spectrum[static_cast<size_t>(band)]
                             - right.spectrum[static_cast<size_t>(band)]);
    return distance;
}

float fullFeatureDistance(const BoundaryFeature& left, const BoundaryFeature& right)
{
    const auto level = std::abs(left.rms - right.rms)
                       / juce::jmax(0.01f, left.rms + right.rms);
    const auto derivative = std::abs(left.derivative - right.derivative)
                            / juce::jmax(0.01f,
                                        left.derivative + right.derivative);
    const auto stereo = 0.5f * std::abs(left.stereoCorrelation
                                        - right.stereoCorrelation);
    return 0.56f * spectrumDistance(left, right)
           + 0.20f * level + 0.16f * derivative + 0.08f * stereo;
}

float similarityScore(const float distance, const float sensitivity = 2.6f)
{
    return 100.0f * std::exp(
        -sensitivity * juce::jmax(0.0f, distance));
}

BoundaryFeature analyseWhole(const juce::AudioBuffer<float>& audio,
                             const double sampleRate)
{
    BoundaryFeature result;
    constexpr int observations = 12;
    const auto window = juce::jlimit(
        64, audio.getNumSamples(),
        juce::roundToInt(sampleRate * 0.24));
    for (int observation = 0; observation < observations; ++observation)
    {
        const auto maximumStart = juce::jmax(0, audio.getNumSamples() - window);
        const auto start = observation * maximumStart
                           / juce::jmax(1, observations - 1);
        const auto feature = analyseBoundary(audio, start, window);
        result.rms += feature.rms;
        result.derivative += feature.derivative;
        result.stereoCorrelation += feature.stereoCorrelation;
        for (int band = 0; band < spectrumBands; ++band)
            result.spectrum[static_cast<size_t>(band)]
                += feature.spectrum[static_cast<size_t>(band)];
    }
    result.rms /= static_cast<float>(observations);
    result.derivative /= static_cast<float>(observations);
    result.stereoCorrelation /= static_cast<float>(observations);
    for (auto& value : result.spectrum)
        value /= static_cast<float>(observations);
    return result;
}

float sampledPercentile(const std::vector<float>& values, const float proportion)
{
    if (values.empty())
        return 0.0f;
    auto copy = values;
    const auto position = static_cast<size_t>(juce::roundToInt(
        juce::jlimit(0.0f, 1.0f, proportion)
        * static_cast<float>(copy.size() - 1u)));
    std::nth_element(copy.begin(), copy.begin() + static_cast<ptrdiff_t>(position),
                     copy.end());
    return copy[position];
}

float frameRms(const juce::AudioBuffer<float>& source,
               const int start,
               const int sampleCount)
{
    auto energy = 0.0;
    for (int sample = 0; sample < sampleCount; ++sample)
        for (int channel = 0; channel < source.getNumChannels(); ++channel)
        {
            const auto value = source.getSample(channel, start + sample);
            energy += static_cast<double>(value) * value;
        }
    return std::sqrt(static_cast<float>(
        energy / static_cast<double>(
            sampleCount * juce::jmax(1, source.getNumChannels()))));
}

struct TextureGrain
{
    int start = 0;
    float rms = 0.0f;
    float envelopeRangeDb = 0.0f;
    BoundaryFeature head;
    BoundaryFeature tail;
};

float grainEnvelopeRangeDb(const juce::AudioBuffer<float>& source,
                           const int start,
                           const int sampleCount)
{
    constexpr int observations = 8;
    const auto blockSamples = juce::jmax(16, sampleCount / observations);
    std::vector<float> levels;
    levels.reserve(observations);
    for (int observation = 0; observation < observations; ++observation)
    {
        const auto offset = juce::jmin(
            sampleCount - blockSamples,
            observation * juce::jmax(0, sampleCount - blockSamples)
                / juce::jmax(1, observations - 1));
        levels.push_back(frameRms(source, start + offset, blockSamples));
    }
    const auto low = juce::jmax(1.0e-8f, sampledPercentile(levels, 0.15f));
    const auto high = juce::jmax(low, sampledPercentile(levels, 0.85f));
    return 20.0f * std::log10(high / low);
}

struct SpatialMeasurement
{
    float correlation = 1.0f;
    float imbalanceDb = 0.0f;
};

SpatialMeasurement analyseSelectedSpatial(
    const juce::AudioBuffer<float>& source,
    const std::vector<int>& frameStarts,
    const std::vector<int>& selectedFrames,
    const int frameSamples)
{
    SpatialMeasurement result;
    if (source.getNumChannels() < 2)
        return result;

    auto leftEnergy = 0.0;
    auto rightEnergy = 0.0;
    auto dot = 0.0;
    for (const auto frame : selectedFrames)
    {
        const auto start = frameStarts[static_cast<size_t>(frame)];
        for (int sample = 0; sample < frameSamples; ++sample)
        {
            const auto left = static_cast<double>(source.getSample(0, start + sample));
            const auto right = static_cast<double>(source.getSample(1, start + sample));
            leftEnergy += left * left;
            rightEnergy += right * right;
            dot += left * right;
        }
    }
    const auto denominator = std::sqrt(leftEnergy * rightEnergy);
    if (denominator > 1.0e-12)
        result.correlation = static_cast<float>(
            juce::jlimit(-1.0, 1.0, dot / denominator));
    result.imbalanceDb = 10.0f * std::log10(static_cast<float>(
        juce::jmax(1.0e-12, leftEnergy) / juce::jmax(1.0e-12, rightEnergy)));
    return result;
}

void applyCircularMacroMovement(juce::AudioBuffer<float>& audio,
                                const float sourceRangeDb,
                                const float flatten,
                                DeterministicRandom& random)
{
    const auto depthDb = juce::jlimit(0.0f, 9.0f, sourceRangeDb)
                         * (1.0f - flatten) * 0.48f;
    if (depthDb < 0.01f || audio.getNumSamples() == 0)
        return;

    constexpr std::array<float, 3> weights { 0.58f, 0.29f, 0.13f };
    std::array<float, weights.size()> phases {};
    std::array<int, weights.size()> cycles {};
    for (size_t layer = 0; layer < weights.size(); ++layer)
    {
        phases[layer] = juce::MathConstants<float>::twoPi * random.unit();
        cycles[layer] = 1 + static_cast<int>(random.next() % static_cast<uint32_t>(2 + layer * 2));
    }

    const auto gainAt = [&] (const int sample)
    {
        const auto position = static_cast<float>(sample)
                              / static_cast<float>(audio.getNumSamples());
        auto movement = 0.0f;
        for (size_t layer = 0; layer < weights.size(); ++layer)
            movement += weights[layer] * std::sin(
                juce::MathConstants<float>::twoPi
                    * static_cast<float>(cycles[layer]) * position
                + phases[layer]);
        return juce::Decibels::decibelsToGain(depthDb * movement);
    };
    auto squareGain = 0.0;
    for (int sample = 0; sample < audio.getNumSamples(); ++sample)
    {
        const auto gain = gainAt(sample);
        squareGain += static_cast<double>(gain) * gain;
    }
    const auto normalisation = 1.0f / std::sqrt(static_cast<float>(
        squareGain / static_cast<double>(audio.getNumSamples())));
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            audio.setSample(channel, sample, audio.getSample(channel, sample)
                * gainAt(sample) * normalisation);
}

void matchStereoImage(juce::AudioBuffer<float>& audio,
                      const SpatialMeasurement source,
                      const float amount)
{
    if (audio.getNumChannels() < 2 || audio.getNumSamples() == 0 || amount <= 0.0f)
        return;

    const auto originalRms = RenderQuality::calculateRms(audio);
    const auto currentCorrelation = RenderQuality::calculateStereoCorrelation(audio);
    const auto desiredCorrelation = juce::jlimit(
        -0.92f, 0.98f, currentCorrelation
            + amount * (source.correlation - currentCorrelation));

    auto midEnergy = 0.0;
    auto sideEnergy = 0.0;
    constexpr auto inverseRootTwo = 0.7071067811865475f;
    for (int sample = 0; sample < audio.getNumSamples(); ++sample)
    {
        const auto left = audio.getSample(0, sample);
        const auto right = audio.getSample(1, sample);
        const auto mid = (left + right) * inverseRootTwo;
        const auto side = (left - right) * inverseRootTwo;
        midEnergy += static_cast<double>(mid) * mid;
        sideEnergy += static_cast<double>(side) * side;
    }
    const auto currentRatio = std::sqrt(static_cast<float>(
        juce::jmax(1.0e-12, sideEnergy) / juce::jmax(1.0e-12, midEnergy)));
    const auto desiredRatio = std::sqrt(
        (1.0f - desiredCorrelation) / (1.0f + desiredCorrelation));
    const auto sideScale = juce::jlimit(0.25f, 4.0f,
        desiredRatio / juce::jmax(1.0e-6f, currentRatio));
    for (int sample = 0; sample < audio.getNumSamples(); ++sample)
    {
        const auto left = audio.getSample(0, sample);
        const auto right = audio.getSample(1, sample);
        const auto mid = (left + right) * inverseRootTwo;
        const auto side = (left - right) * inverseRootTwo * sideScale;
       ◊ç∑Ê⁄$z{-ÆÈ‹j◊ù7C∆ñÁC‚Üw&ñÁ2Á6ó¶RÇíì≤≤∂w&ñ‚ê¢∞¢7F&∆Tw&ñÁ2ÁW6Öˆ&6≤Üw&ñ‚ì∞¢7F&∆T∆WfV«2ÁW6Öˆ&6≤Üw&ñÁ5∑7FFñ5ˆ67C«6ó¶U˜C‚Üw&ñ‚ï“Á&◊2ì∞¢–¢–¢6ˆÁ7BWFÚF&vWDw&ñÂ&◊2“ßV6S£¶¶÷ÇÄ¢„R”Üb¬6◊∆VEW&6VÁFñ∆Rá7F&∆T∆WfV«2¬„Sbíì∞†¢7FC£ßfV7F˜#∆ñÁC‚˜WGWE˜6óFñˆÁ2á7FFñ5ˆ67C«6ó¶U˜C‚Ü˜WGWDw&ñ‰6˜VÁBíì∞¢6ˆÁ7BWFÚ˜6óFñˆ‰¶óGFW"“„sVb¢6WGFñÊw2Áf&ñFñˆ‡¢¢7FFñ5ˆ67C∆f∆ˆC‚ÜfW&vU7FWì∞¢f˜"ÜñÁBw&ñ‚“≤w&ñ‚¬˜WGWDw&ñ‰6˜VÁC≤≤∂w&ñ‚ê¢∞¢6ˆÁ7BWFÚ6ó&7V∆"“7FFñ5ˆ67C∆f∆ˆC‚Üw&ñ‚ê¢Ú7FFñ5ˆ67C∆f∆ˆC‚Ü˜WGWDw&ñ‰6˜VÁBì∞¢6ˆÁ7BWFÚ¶óGFW"“˜6óFñˆ‰¶óGFW ¢¢É„cÜb¢7FC£ß6ñ‚ÜßV6S£§÷FÑ6ˆÁ7FÁG3∆f∆ˆC„£ßGvıí¢6ó&7V∆"ê¢≤„3&b¢7FC£ß6ñ‚Ä¢2„b¢ßV6S£§÷FÑ6ˆÁ7FÁG3∆f∆ˆC„£ßGvıí¢6ó&7V∆"íì∞¢˜WGWE˜6óFñˆÁ5∑7FFñ5ˆ67C«6ó¶U˜C‚Üw&ñ‚ï““ßV6S£¶¶∆ñ÷óBÄ¢¬6ˆÁ7G'V7FñˆÂ6◊∆W2“¿¢ßV6S£ß&˜VÊEFÙñÁBÜ6ó&7V∆"¢7FFñ5ˆ67C∆f∆ˆC‚Ü6ˆÁ7G'V7FñˆÂ6◊∆W2ê¢≤¶óGFW"íì∞¢–†¢ßV6S£§VFñÙ'VffW#∆f∆ˆC‚76V÷&∆VBá6˜W&6RÊvWDÁV‘6ÜÊÊV«2Çí¬6ˆÁ7G'V7FñˆÂ6◊∆W2ì∞¢76V÷&∆VBÊ6∆V"Çì∞¢7FC£ßfV7F˜#∆f∆ˆC‚Ê˜&÷∆ó6Fñˆ‚á7FFñ5ˆ67C«6ó¶U˜C‚Ü6ˆÁ7G'V7FñˆÂ6◊∆W2í¬„bì∞¢7FC£ßfV7F˜#∆ñÁC‚&V6VÁDw&ñÁ3∞¢7FC£ßfV7F˜#∆ñÁC‚w&ñÂW6T6˜VÁG2Üw&ñÁ2Á6ó¶RÇí¬ì∞¢WFÚ&Wfñ˜W4w&ñ‚“”∞¢WFÚfó'7Dw&ñ‚“”∞¢f˜"ÜñÁB˜WGWDw&ñ‚“≤˜WGWDw&ñ‚¬˜WGWDw&ñ‰6˜VÁC≤≤∂˜WGWDw&ñ‚ê¢∞¢WFÚ6V∆V7FVB“7F&∆Tw&ñÁ5∑7FFñ5ˆ67C«6ó¶U˜C‚Ä¢&ÊFˆ“ÊÊWáBÇíR7FFñ5ˆ67C«VñÁC3%˜C‚á7F&∆Tw&ñÁ2Á6ó¶RÇííï”∞¢ñbá&Wfñ˜W4w&ñ‚„“bb7F&∆Tw&ñÁ2Á6ó¶RÇí‚Rê¢∞¢WFÚ&W7E66˜&R“7FC£¶ÁV÷W&ñ5ˆ∆ñ÷óG3∆f∆ˆC„£¶÷ÇÇì∞¢f˜"Ü6ˆÁ7BWFÚ6ÊFñFFTñÊFWÇ¢7F&∆Tw&ñÁ2ê¢∞¢6ˆÁ7BWFÚb&Wfñ˜W2“w&ñÁ5∑7FFñ5ˆ67C«6ó¶U˜C‚á&Wfñ˜W4w&ñ‚ï”∞¢6ˆÁ7BWFÚb6ÊFñFFR“w&ñÁ5∑7FFñ5ˆ67C«6ó¶U˜C‚Ü6ÊFñFFTñÊFWÇï”∞¢WFÚ66˜&R“„cÜb¢gV∆ƒfVGW&TFó7FÊ6RÄ¢&Wfñ˜W2ÁFñ¬¬6ÊFñFFRÊÜVBì∞¢66˜&R≥“„&b¢7FC£¶'2á7FC£¶∆ˆrÄ¢ßV6S£¶¶÷ÇÉ„R”Üb¬&Wfñ˜W2Á&◊2ê¢ÚßV6S£¶¶÷ÇÉ„R”Üb¬6ÊFñFFRÁ&◊2ííì∞¢66˜&R≥“„fb¢ßV6S£¶¶∆ñ÷óBÄ¢„b¬„b¬6ÊFñFFRÊVÁfV∆˜U&ÊvTF"ÚÇ„bê¢¢6WGFñÊw2Êf∆GFV„∞¢ñbá7FC£¶'2Ü6ÊFñFFRÁ7F'B“&Wfñ˜W2Á7F'Bê¢¬w&ñÂ6◊∆W2Ú"ê¢66˜&R≥“„SVc∞¢ñbá7FC£¶'2Ü6ÊFñFFRÁ7F'@¢“á&Wfñ˜W2Á7F'B≤fW&vU7FWíê¢¬w&ñÂ6◊∆W2Ú2ê¢66˜&R≥“„3b¢6WGFñÊw2Êf∆GFV„∞¢ñbá7FC£¶fñÊBá&V6VÁDw&ñÁ2Ê&Vvñ‚Çí¬&V6VÁDw&ñÁ2ÊVÊBÇí¿¢6ÊFñFFTñÊFWÇí“&V6VÁDw&ñÁ2ÊVÊBÇíê¢66˜&R≥“„CÜc∞¢ñbÜ˜WGWDw&ñ‚”“˜WGWDw&ñ‰6˜VÁB“bbfó'7Dw&ñ‚„“ê¢66˜&R≥“„s&b¢gV∆ƒfVGW&TFó7FÊ6RÄ¢6ÊFñFFRÁFñ¬¿¢w&ñÁ5∑7FFñ5ˆ67C«6ó¶U˜C‚Üfó'7Dw&ñ‚ï“ÊÜVBì∞¢66˜&R≥“„Üb¢7FFñ5ˆ67C∆f∆ˆC‚Ä¢w&ñÂW6T6˜VÁG5∑7FFñ5ˆ67C«6ó¶U˜C‚Ü6ÊFñFFTñÊFWÇï“ì∞¢66˜&R≥“&ÊFˆ“ÁVÊóBÇí¢É„vb≤„#Üb¢6WGFñÊw2Áf&ñFñˆ‚ì∞¢ñbá66˜&R¬&W7E66˜&Rê¢∞¢&W7E66˜&R“66˜&S∞¢6V∆V7FVB“6ÊFñFFTñÊFWÉ∞¢–¢–¢–†¢6ˆÁ7BWFÚbw&ñ‚“w&ñÁ5∑7FFñ5ˆ67C«6ó¶U˜C‚á6V∆V7FVBï”∞¢6ˆÁ7BWFÚvñ‚“ßV6S£¶¶∆ñ÷óBÄ¢„#Vb¬B„b¬7FC£ß˜rÄ¢F&vWDw&ñÂ&◊2ÚßV6S£¶¶÷ÇÉ„R”Üb¬w&ñ‚Á&◊2í¿¢„ìb¢6WGFñÊw2Êf∆GFV‚íì∞¢6ˆÁ7BWFÚ˜WGWE7F'B“˜WGWE˜6óFñˆÁ5∑7FFñ5ˆ67C«6ó¶U˜C‚Ü˜WGWDw&ñ‚ï”∞¢6ˆÁ7BWFÚ&Wfñ˜W5˜6óFñˆ‚“˜WGWDw&ñ‚”“ ¢Ú˜WGWE˜6óFñˆÁ2Ê&6≤Çí“6ˆÁ7G'V7FñˆÂ6◊∆W0¢¢˜WGWE˜6óFñˆÁ5∑7FFñ5ˆ67C«6ó¶U˜C‚Ü˜WGWDw&ñ‚“ï”∞¢6ˆÁ7BWFÚÊWáE˜6óFñˆ‚“˜WGWDw&ñ‚≤”“˜WGWDw&ñ‰6˜VÁ@¢Ú6ˆÁ7G'V7FñˆÂ6◊∆W0¢¢˜WGWE˜6óFñˆÁ5∑7FFñ5ˆ67C«6ó¶U˜C‚Ü˜WGWDw&ñ‚≤ï”∞¢6ˆÁ7BWFÚfFTñÂ6◊∆W2“ßV6S£¶¶∆ñ÷óBÄ¢¬w&ñÂ6◊∆W2Ú2¿¢w&ñÂ6◊∆W2“Ü˜WGWE7F'B“&Wfñ˜W5˜6óFñˆ‚íì∞¢6ˆÁ7BWFÚfFT˜WE6◊∆W2“ßV6S£¶¶∆ñ÷óBÄ¢¬w&ñÂ6◊∆W2Ú2¿¢w&ñÂ6◊∆W2“ÜÊWáE˜6óFñˆ‚“˜WGWE7F'Bíì∞¢f˜"ÜñÁB6◊∆R“≤6◊∆R¬w&ñÂ6◊∆W3≤≤∑6◊∆Rê¢∞¢WFÚvñÊF˜r“„c∞¢ñbá6◊∆R¬fFTñÂ6◊∆W2ê¢∞¢6ˆÁ7BWFÚÜ6R“ßV6S£§÷FÑ6ˆÁ7FÁG3∆f∆ˆC„£¶Ü∆eê¢¢á7FFñ5ˆ67C∆f∆ˆC‚á6◊∆Rí≤„Vbê¢Ú7FFñ5ˆ67C∆f∆ˆC‚ÜfFTñÂ6◊∆W2ì∞¢6ˆÁ7BWFÚ6ñÊR“7FC£ß6ñ‚áÜ6Rì∞¢vñÊF˜r“6ñÊR¢6ñÊS∞¢–¢V«6Rñbá6◊∆R„“w&ñÂ6◊∆W2“fFT˜WE6◊∆W2ê¢∞¢6ˆÁ7BWFÚˆfg6WB“6◊∆R“Üw&ñÂ6◊∆W2“fFT˜WE6◊∆W2ì∞¢6ˆÁ7BWFÚÜ6R“ßV6S£§÷FÑ6ˆÁ7FÁG3∆f∆ˆC„£¶Ü∆eê¢¢á7FFñ5ˆ67C∆f∆ˆC‚Üˆfg6WBí≤„Vbê¢Ú7FFñ5ˆ67C∆f∆ˆC‚ÜfFT˜WE6◊∆W2ì∞¢6ˆÁ7BWFÚ6˜6ñÊR“7FC£¶6˜2áÜ6Rì∞¢vñÊF˜r“6˜6ñÊR¢6˜6ñÊS∞¢–¢6ˆÁ7BWFÚ˜6óFñˆ‚“Ü˜WGWE7F'B≤6◊∆RíR6ˆÁ7G'V7FñˆÂ6◊∆W3∞¢f˜"ÜñÁB6ÜÊÊV¬“≤6ÜÊÊV¬¬76V÷&∆VBÊvWDÁV‘6ÜÊÊV«2Çì≤≤∂6ÜÊÊV¬ê¢76V÷&∆VBÊFE6◊∆RÄ¢6ÜÊÊV¬¬˜6óFñˆ‚¿¢6˜W&6RÊvWE6◊∆RÜ6ÜÊÊV¬¬w&ñ‚Á7F'B≤6◊∆Rí¢vñ‚¢vñÊF˜rì∞¢Ê˜&÷∆ó6FñˆÂ∑7FFñ5ˆ67C«6ó¶U˜C‚á˜6óFñˆ‚ï“≥“vñÊF˜s∞¢–†¢&Wfñ˜W4w&ñ‚“6V∆V7FVC∞¢ñbÜfó'7Dw&ñ‚¬ê¢fó'7Dw&ñ‚“6V∆V7FVC∞¢≤∂w&ñÂW6T6˜VÁG5∑7FFñ5ˆ67C«6ó¶U˜C‚á6V∆V7FVBï”∞¢&V6VÁDw&ñÁ2ÁW6Öˆ&6≤á6V∆V7FVBì∞¢ñbá&V6VÁDw&ñÁ2Á6ó¶RÇí‚WRê¢&V6VÁDw&ñÁ2ÊW&6Rá&V6VÁDw&ñÁ2Ê&Vvñ‚Çíì∞¢–¢f˜"ÜñÁB6◊∆R“≤6◊∆R¬6ˆÁ7G'V7FñˆÂ6◊∆W3≤≤∑6◊∆Rê¢∞¢6ˆÁ7BWFÚ66∆R“„bÚßV6S£¶¶÷ÇÄ¢„R”fb¬Ê˜&÷∆ó6FñˆÂ∑7FFñ5ˆ67C«6ó¶U˜C‚á6◊∆Rï“ì∞¢f˜"ÜñÁB6ÜÊÊV¬“≤6ÜÊÊV¬¬76V÷&∆VBÊvWDÁV‘6ÜÊÊV«2Çì≤≤∂6ÜÊÊV¬ê¢76V÷&∆VBÁ6WE6◊∆RÜ6ÜÊÊV¬¬6◊∆R¿¢76V÷&∆VBÊvWE6◊∆RÜ6ÜÊÊV¬¬6◊∆Rí¢66∆Rì∞¢–†¢WFÚ&V6ˆÁ7G'V7FVB“7ñÁFÜW6ó¶T÷FW&ñƒ÷ˆFV¬Ä¢Ê«ó6ó57V7G&¬÷ˆFV«2¬6˜W&6RÊvWDÁV‘6ÜÊÊV«2Çí¬fgD˜&FW"¿¢6ˆÁ7G'V7FñˆÂ6◊∆W2¬6WGFñÊw2Áf&ñFñˆ‚¬&ÊFˆ“ì∞¢6ˆÁ7BWFÚWÜV◊∆%&◊2“&VÊFW%V∆óGì£¶6∆7V∆FU&◊2Ü76V÷&∆VBì∞¢6ˆÁ7BWFÚ&V6ˆÁ7G'V7FVE&◊2“&VÊFW%V∆óGì£¶6∆7V∆FU&◊2á&V6ˆÁ7G'V7FVBì∞¢ñbá&V6ˆÁ7G'V7FVE&◊2‚„R”ñbbbWÜV◊∆%&◊2‚„R”ñbê¢&V6ˆÁ7G'V7FVBÊ«îvñ‚ÜWÜV◊∆%&◊2Ú&V6ˆÁ7G'V7FVE&◊2ì∞†¢&W7V«BÊVFñÚÁ6WE6ó¶Rá6˜W&6RÊvWDÁV‘6ÜÊÊV«2Çí¬6ˆÁ7G'V7FñˆÂ6◊∆W2ì∞¢6ˆÁ7BWFÚ&V'Vñ∆B“6WGFñÊw2Á6˜W&6T÷F6É∞¢6ˆÁ7BWFÚWÜV◊∆$vñ‚“7FC£¶6˜2Ä¢&V'Vñ∆B¢ßV6S£§÷FÑ6ˆÁ7FÁG3∆f∆ˆC„£¶Ü∆eíì∞¢6ˆÁ7BWFÚ&V6ˆÁ7G'V7Fñˆ‰vñ‚“7FC£ß6ñ‚Ä¢&V'Vñ∆B¢ßV6S£§÷FÑ6ˆÁ7FÁG3∆f∆ˆC„£¶Ü∆eíì∞¢f˜"ÜñÁB6ÜÊÊV¬“≤6ÜÊÊV¬¬&W7V«BÊVFñÚÊvWDÁV‘6ÜÊÊV«2Çì≤≤∂6ÜÊÊV¬ê¢f˜"ÜñÁB6◊∆R“≤6◊∆R¬6ˆÁ7G'V7FñˆÂ6◊∆W3≤≤∑6◊∆Rê¢&W7V«BÊVFñÚÁ6WE6◊∆RÄ¢6ÜÊÊV¬¬6◊∆R¿¢WÜV◊∆$vñ‚¢76V÷&∆VBÊvWE6◊∆RÜ6ÜÊÊV¬¬6◊∆Rê¢≤&V6ˆÁ7G'V7Fñˆ‰vñ‚¢&V6ˆÁ7G'V7FVBÊvWE6◊∆RÜ6ÜÊÊV¬¬6◊∆Ríì∞¢f∆GFV‰6ó&7V∆$VÁfV∆˜Rá&W7V«BÊVFñÚ¬6◊∆U&FR¬6WGFñÊw2Êf∆GFV‚ì∞†¢÷F6Ñ÷FW&ñƒ6ˆ∆˜W"á&W7V«BÊVFñÚ¬6ˆ∆˜W$÷ˆFV«5≥“¬fgD˜&FW"¬6◊∆U&FR¿¢„s&b≤„#b¢6WGFñÊw2Á6˜W&6T÷F6Çì∞†¢«î6ó&7V∆$÷7&Ù÷˜fV÷VÁBá&W7V«BÊVFñÚ¬7FófU&ÊvTF"¬6WGFñÊw2Êf∆GFV‚¬&ÊFˆ“ì∞¢&W7V«BÊVFñÚ“6∆˜6T6ˆÁ7G'V7FVD∆ˆ˜á&W7V«BÊVFñÚ¬6◊∆U&FR¬6∆˜7W&T˜fW&∆ì∞¢&W7V«BÊ6ˆÁFñÁ4ˆÊ«îfñÊóFU6◊∆W2“&VÊFW%V∆óGì£ß&Wó$Êˆ‰fñÊóFTÊE&V÷˜fTF2Ä¢&W7V«BÊVFñÚì∞†¢6ˆÁ7BWFÚ6˜W&6T∆˜VFÊW72“&VÊFW%V∆óGì£¶W7Fñ÷FTñÁFVw&FVD∆˜VFÊW74F"Ä¢7FófU&VfW&VÊ6R¬6◊∆U&FRì∞¢6ˆÁ7BWFÚ&t∆˜VFÊW72“&VÊFW%V∆óGì£¶W7Fñ÷FTñÁFVw&FVD∆˜VFÊW74F"Ä¢&W7V«BÊVFñÚ¬6◊∆U&FRì∞¢6ˆÁ7BWFÚ&t6˜'&V∆Fñˆ‚“&VÊFW%V∆óGì£¶6∆7V∆FU7FW&VÙ6˜'&V∆Fñˆ‚á&W7V«BÊVFñÚì∞¢6ˆÁ7BWFÚ&tñ÷&∆Ê6R“&VÊFW%V∆óGì£¶6∆7V∆FU7FW&VÙ∆WfVƒñ÷&∆Ê6TF"á&W7V«BÊVFñÚì∞¢6ˆÁ7BWFÚ7Fñƒ÷F6Ç“„3Vb≤„CVb¢É„b“6WGFñÊw2Á6˜W&6T÷F6Çì∞¢6ˆÁ7BWFÚWáV7FVD6˜'&V∆Fñˆ‚“&t6˜'&V∆Fñˆ‚≤7Fñƒ÷F6Ä¢¢á6˜W&6U7Fñ¬Ê6˜'&V∆Fñˆ‚“&t6˜'&V∆Fñˆ‚ì∞¢6ˆÁ7BWFÚWáV7FVDñ÷&∆Ê6R“&tñ÷&∆Ê6R≤7Fñƒ÷F6Ä¢¢á6˜W&6U7Fñ¬Êñ÷&∆Ê6TF"“&tñ÷&∆Ê6Rì∞¢÷F6Ö7FW&VÙñ÷vRá&W7V«BÊVFñÚ¬6˜W&6U7Fñ¬¬7Fñƒ÷F6Çì∞†¢6ˆÁ7BWFÚ&WVW7FVDvñ‰F"“ßV6S£¶¶∆ñ÷óBÄ¢”Ç„b¬Ç„b¬6˜W&6T∆˜VFÊW72“&t∆˜VFÊW72ì∞¢6ˆÁ7BWFÚ&uG'VUV≤“&VÊFW%V∆óGì£¶W7Fñ÷FT6ó&7V∆%G'VUV≤á&W7V«BÊVFñÚì∞¢6ˆÁ7BWFÚfñ∆&∆TÜVG&ˆˆ‘F"“”„b“ßV6S£§FV6ñ&V«3£¶vñÂFÙFV6ñ&V«2Ä¢ßV6S£¶¶÷ÇÉ„R”ñb¬&uG'VUV≤íì∞¢6ˆÁ7BWFÚ÷F6ÜVDvñ‰F"“ßV6S£¶¶÷ñ‚á&WVW7FVDvñ‰F"¬fñ∆&∆TÜVG&ˆˆ‘F"ì∞¢&W7V«BÊVFñÚÊ«îvñ‚ÜßV6S£§FV6ñ&V«3£¶FV6ñ&V«5FÙvñ‚Ü÷F6ÜVDvñ‰F"íì∞¢6ˆÁ7BWFÚWáV7FVD∆˜VFÊW72“&t∆˜VFÊW72≤÷F6ÜVDvñ‰F#∞¢&W7V«BÁG'VUV¥F'G“&VÊFW%V∆óGì£¶«î6ó&7V∆%G'VUV¥6Vñ∆ñÊrá&W7V«BÊVFñÚ¬”„bì∞†¢6ˆÁ7BWFÚfVGW&U6◊∆W2“ßV6S£¶¶∆ñ÷óBÄ¢cB¬F&vWE6◊∆W2Ú"¬ßV6S£ß&˜VÊEFÙñÁBá6◊∆U&FR¢„Bíì∞¢6ˆÁ7BWFÚÜVB“Ê«ó6T&˜VÊF'íá&W7V«BÊVFñÚ¬¬fVGW&U6◊∆W2ì∞¢6ˆÁ7BWFÚFñ¬“Ê«ó6T&˜VÊF'íÄ¢&W7V«BÊVFñÚ¬F&vWE6◊∆W2“fVGW&U6◊∆W2¬fVGW&U6◊∆W2ì∞¢WFÚ&˜VÊF'îßV◊“„c∞¢WFÚ&˜VÊF'ï&VfW&VÊ6R“„c∞¢f˜"ÜñÁB6ÜÊÊV¬“≤6ÜÊÊV¬¬&W7V«BÊVFñÚÊvWDÁV‘6ÜÊÊV«2Çì≤≤∂6ÜÊÊV¬ê¢∞¢&˜VÊF'îßV◊“ßV6S£¶¶÷ÇÄ¢&˜VÊF'îßV◊¿¢7FC£¶'2á&W7V«BÊVFñÚÊvWE6◊∆RÜ6ÜÊÊV¬¬F&vWE6◊∆W2“ê¢“&W7V«BÊVFñÚÊvWE6◊∆RÜ6ÜÊÊV¬¬ííì∞¢&˜VÊF'ï&VfW&VÊ6R“ßV6S£¶¶÷ÇÄ¢&˜VÊF'ï&VfW&VÊ6R¿¢&W7V«BÊVFñÚÊvWE$’4∆WfV¬Ü6ÜÊÊV¬¬¬F&vWE6◊∆W2íì∞¢–¢ßV6S£¶ñvÊ˜&UVÁW6VBÜ&˜VÊF'ï&VfW&VÊ6Rì∞¢6ˆÁ7BWFÚÊGW&≈7FW“ßV6S£¶¶÷ÇÄ¢„R”fb¬„Vb¢ÜÜVBÊFW&ófFófR≤Fñ¬ÊFW&ófFófRíì∞¢6ˆÁ7BWFÚWÜ6W757FW“ßV6S£¶¶÷ÇÄ¢„b¬&˜VÊF'îßV◊ÚÊGW&≈7FW“„3Vbì∞¢6ˆÁ7BWFÚßV◊V∆óGí“„b¢7FC£¶WáÇ”„ìb¢WÜ6W757FWì∞¢&W7V«BÊ6∆˜7W&UV∆óGí“„C&b¢6ñ÷ñ∆&óGï66˜&RÄ¢gV∆ƒfVGW&TFó7FÊ6RáFñ¬¬ÜVBí¬"„bê¢≤„SÜb¢ßV◊V∆óGì∞¢&W7V«BÁG&Á6óFñˆÂV∆óGí“6∆7V∆FU7FFñˆÊ&óGíá&W7V«BÊVFñÚ¬6◊∆U&FRì∞†¢6ˆÁ7BWFÚ6˜W&6TfVGW&R“Ê«ó6UvÜˆ∆Rá6˜W&6R¬6◊∆U&FRì∞¢6ˆÁ7BWFÚ˜WGWDfVGW&R“Ê«ó6UvÜˆ∆Rá&W7V«BÊVFñÚ¬6◊∆U&FRì∞¢7FC£ßfV7F˜#«7FC£ßfV7F˜#∆f∆ˆC„‚˜WGWE7V7G&∞¢6ˆÁ7FWá"ñÁB˜WGWD÷ˆFVƒg&÷W2“#C∞¢˜WGWE7V7G&Á&W6W'fRÜ˜WGWD÷ˆFVƒg&÷W2ì∞¢6ˆÁ7BWFÚ˜WGWD÷Üñ◊V’7F'B“ßV6S£¶¶÷ÇÉ¬F&vWE6◊∆W2“fgE6ó¶Rì∞¢f˜"ÜñÁBg&÷R“≤g&÷R¬˜WGWD÷ˆFVƒg&÷W3≤≤∂g&÷Rê¢∞¢6ˆÁ7BWFÚ7F'B“g&÷R¢˜WGWD÷Üñ◊V’7F'BÚÜ˜WGWD÷ˆFVƒg&÷W2“ì∞¢˜WGWE7V7G&ÁW6Öˆ&6≤ÜÊ«ó6Tg&÷U7V7G'V“Ä¢&W7V«BÊVFñÚ¬¬7F'B¬fgE6ó¶R¬fgBíì∞¢–¢6ˆÁ7BWFÚ6ˆ'6U7V7G'V““6ˆ◊&U7V7G&ƒ÷ˆFV«2Ä¢6ˆ∆˜W$÷ˆFV«5≥“¬'Vñ∆D÷FW&ñƒ6ˆ∆˜W$÷ˆFV¬Ü˜WGWE7V7G&í¬fgE6ó¶R¬6◊∆U&FRì∞¢6ˆÁ7BWFÚ6˜W&6Tg&÷TñFVÁFóGí“6ˆ◊&Tg&÷TñFVÁFóGíÄ¢Ê«ó6ó57V7G&≥“¬˜WGWE7V7G&ì∞¢ÚÚ&V'Vñ«BFWáGW&W2÷íñÁFVÁFñˆÊ∆«í&ÊFˆ‚FÜR6˜W&6RWfVÁBFñ÷V∆ñÊRÊBWÜ7Bg&÷W2‡¢ÚÚ6ˆ'6R÷FW&ñ¬6ˆ∆˜W"&V÷ñÁ2÷ÊFF˜'í6ÚFˆÊ¬˜"w&ÁV∆"6˜W&6R6ÊÊ˜B6ˆ∆∆6RF¢ÚÚvVÊW&ñ2vÜóFR˜ñÊ≤Êˆó6R‡¢&W7V«BÁ7V7G'V’&W6W'fFñˆ‚“„s&b¢6ˆ'6U7V7G'V–¢≤„#Üb¢6˜W&6Tg&÷TñFVÁFóGì∞¢6ˆÁ7BWFÚ˜WGWD∆˜VFÊW72“&VÊFW%V∆óGì£¶W7Fñ÷FTñÁFVw&FVD∆˜VFÊW74F"Ä¢&W7V«BÊVFñÚ¬6◊∆U&FRì∞¢6ˆÁ7BWFÚ˜WGWD6˜'&V∆Fñˆ‚“&VÊFW%V∆óGì£¶6∆7V∆FU7FW&VÙ6˜'&V∆Fñˆ‚á&W7V«BÊVFñÚì∞¢6ˆÁ7BWFÚ˜WGWDñ÷&∆Ê6R“&VÊFW%V∆óGì£¶6∆7V∆FU7FW&VÙ∆WfVƒñ÷&∆Ê6TF"á&W7V«BÊVFñÚì∞¢6ˆÁ7BWFÚ∆˜VFÊW74W'&˜$F"“7FC£¶'2Ü˜WGWD∆˜VFÊW72“WáV7FVD∆˜VFÊW72ì∞¢&W7V«BÊ∆˜VFÊW75&W6W'fFñˆ‚“„b¢7FC£¶WáÇ”„3b¢∆˜VFÊW74W'&˜$F"ì∞¢&W7V«BÁÜ6U&W6W'fFñˆ‚“„b¢7FC£¶WáÄ¢”2„&b¢7FC£¶'2Ü˜WGWD6˜'&V∆Fñˆ‚“WáV7FVD6˜'&V∆Fñˆ‚íì∞¢&W7V«BÁ˜6óFñˆÂ&W6W'fFñˆ‚“&W7V«BÊVFñÚÊvWDÁV‘6ÜÊÊV«2Çí¬"Ú„`¢¢„b¢7FC£¶WáÇ”„#Fb¢7FC£¶'2Ü˜WGWDñ÷&∆Ê6R“WáV7FVDñ÷&∆Ê6Ríì∞¢&W7V«BÁ7FW&Vı&W6W'fFñˆ‚“„SÜb¢&W7V«BÁÜ6U&W6W'fFñˆ‡¢≤„C&b¢&W7V«BÁ˜6óFñˆÂ&W6W'fFñˆ„∞¢6ˆÁ7BWFÚ6˜W&6UFWáGW&U&FR“6˜W&6TfVGW&RÊFW&ófFófP¢ÚßV6S£¶¶÷ÇÉ„R”fb¬6˜W&6TfVGW&RÁ&◊2ì∞¢6ˆÁ7BWFÚ˜WGWEFWáGW&U&FR“˜WGWDfVGW&RÊFW&ófFófP¢ÚßV6S£¶¶÷ÇÉ„R”fb¬˜WGWDfVGW&RÁ&◊2ì∞¢&W7V«BÁG&Á6ñVÁE&W6W'fFñˆ‚“„b¢7FC£¶WáÄ¢”„s&b¢7FC£¶'2á7FC£¶∆ˆrÄ¢ßV6S£¶¶÷ÇÉ„R”Fb¬˜WGWEFWáGW&U&FRê¢ÚßV6S£¶¶÷ÇÉ„R”Fb¬6˜W&6UFWáGW&U&FRíííì∞¢&W7V«BÊ÷7&ı7F&ñ∆óGí“6∆7V∆FT÷7&ı7F&ñ∆óGíá&W7V«BÊVFñÚ¬6◊∆U&FRì∞¢6ˆÁ7BWFÚ&WVE&ó6≤“6∆7V∆FU&WVE&ó6≤á&W7V«BÊVFñÚ¬6◊∆U&FRì∞¢&W7V«BÁ&WVE6fWGí“„b¢É„b“&WVE&ó6≤ì∞¢&W7V«BÊFófW'6óGí“&W7V«BÁ&WVE6fWGì∞¢&W7V«BÁV∆óGï66˜&R“„fb¢&W7V«BÊ6∆˜7W&UV∆óGê¢≤„fb¢&W7V«BÁG&Á6óFñˆÂV∆óGê¢≤„#fb¢&W7V«BÁ7V7G'V’&W6W'fFñˆ‡¢≤„&b¢&W7V«BÊ∆˜VFÊW75&W6W'fFñˆ‡¢≤„b¢&W7V«BÁÜ6U&W6W'fFñˆ‡¢≤„Üb¢&W7V«BÁ˜6óFñˆÂ&W6W'fFñˆ‡¢≤„&b¢&W7V«BÊ÷7&ı7F&ñ∆óGê¢≤„b¢&W7V«BÁ&WVE6fWGì∞¢6ˆÁ7BWFÚ&WVó&VE7F&ñ∆óGí“CR„b≤#"„b¢6WGFñÊw2Êf∆GFV„∞¢&W7V«BÁ76VEV∆óGîvFR“&W7V«BÊ6ˆÁFñÁ4ˆÊ«îfñÊóFU6◊∆W0¢bb&W7V«BÁG'VUV¥F'G√“”„ÉV`¢bb&W7V«BÊ6∆˜7W&UV∆óGí„“c„`¢bb&W7V«BÁ7V7G'V’&W6W'fFñˆ‚„“cB„`¢bb&W7V«BÊ∆˜VFÊW75&W6W'fFñˆ‚„“s„`¢bb&W7V«BÁÜ6U&W6W'fFñˆ‚„“c"„`¢bb&W7V«BÁ˜6óFñˆÂ&W6W'fFñˆ‚„“c"„`¢bb&W7V«BÊ÷7&ı7F&ñ∆óGí„“&WVó&VE7F&ñ∆óGê¢bb&W7V«BÁ&WVE6fWGí„“SÇ„c∞¢&WGW&‚&W7V«C∞ß–