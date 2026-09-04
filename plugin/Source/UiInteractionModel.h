#pragma once

#include <algorithm>
#include <cmath>

namespace LoopSurgeonUi
{
enum class StripPathKind
{
    join,
    motion,
    extra
};

struct NormalisedPoint
{
    float x = 0.0f;
    float y = 0.0f;
};

inline float clamp01(const float value) noexcept
{
    return std::clamp(value, 0.0f, 1.0f);
}

inline float smoothstep(const float value) noexcept
{
    const auto t = clamp01(value);
    return t * t * (3.0f - 2.0f * t);
}

inline float advanceTransition(const float current, const float target,
                               const float deltaSeconds,
                               const float durationSeconds) noexcept
{
    if (durationSeconds <= 0.0f)
        return target;

    const auto step = clamp01(deltaSeconds / durationSeconds);
    const auto cubicEaseOut = 1.0f - std::pow(1.0f - step, 3.0f);
    return current + (target - current) * cubicEaseOut;
}

inline NormalisedPoint stripPoint(const StripPathKind kind,
                                  const float normalised) noexcept
{
    const auto t = clamp01(normalised);
    const auto oneMinusT = 1.0f - t;

    NormalisedPoint control;
    NormalisedPoint end;
    switch (kind)
    {
        case StripPathKind::join:
            control = { 0.48f, 0.58f };
            end = { 1.0f, 0.66f };
            break;
        case StripPathKind::motion:
            control = { 0.52f, 0.54f };
            end = { 1.0f, 0.64f };
            break;
        case StripPathKind::extra:
            control = { 0.50f, 0.24f };
            end = { 1.0f, 0.54f };
            break;
    }

    constexpr NormalisedPoint start { 0.0f, 0.42f };
    return {
        oneMinusT * oneMinusT * start.x
            + 2.0f * oneMinusT * t * control.x + t * t * end.x,
        oneMinusT * oneMinusT * start.y
            + 2.0f * oneMinusT * t * control.y + t * t * end.y
    };
}

inline NormalisedPoint stripTangent(const StripPathKind kind,
                                    const float normalised) noexcept
{
    const auto before = stripPoint(kind, normalised - 0.002f);
    const auto after = stripPoint(kind, normalised + 0.002f);
    const auto dx = after.x - before.x;
    const auto dy = after.y - before.y;
    const auto length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.000001f)
        return { 1.0f, 0.0f };
    return { dx / length, dy / length };
}
}
