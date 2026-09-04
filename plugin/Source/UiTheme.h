#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The UI is drawn in this design space, independently of audio parameter state.
namespace LoopSurgeonTheme
{
inline constexpr float width = 1360.0f;
inline constexpr float height = 880.0f;
inline constexpr juce::uint32 background = 0xff17191c;
inline constexpr juce::uint32 surface = 0xff202327;
inline constexpr juce::uint32 inset = 0xff1a1d20;
inline constexpr juce::uint32 line = 0xff363b40;
inline constexpr juce::uint32 text = 0xffeee9e0;
inline constexpr juce::uint32 secondary = 0xffa6aaa9;
inline constexpr juce::uint32 accent = 0xffd4a17d;
inline constexpr juce::uint32 selected = 0xff3b302a;
}
