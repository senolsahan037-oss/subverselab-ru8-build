cmake_minimum_required(VERSION 3.22)

# DEV shell (phase 3): native arm64 for speed. Pass -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" for a universal build.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
endif()
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

project(RU8 VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OFF: the build never writes into ~/Library/Audio/Plug-Ins.
option(SVL_COPY_PLUGINS "Copy the built plug-ins into ~/Library/Audio/Plug-Ins after each build" OFF)

# JUCE is fetched by CMake. To build offline against a checkout already on disk,
# pass -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.1
)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
    set(SVL_RU8_FORMATS Standalone VST3 AU)
else()
    set(SVL_RU8_FORMATS Standalone VST3)
endif()
if(APPLE AND SVL_COPY_PLUGINS)
    set(SVL_RU8_COPY_AFTER_BUILD TRUE)
else()
    set(SVL_RU8_COPY_AFTER_BUILD FALSE)
endif()

juce_add_plugin(RU8
    COMPANY_NAME "SubverseLab"
    PLUGIN_MANUFACTURER_CODE Svlb
    PLUGIN_CODE Ru8d
    PLUGIN_NAME "RU-8"
    PRODUCT_NAME "RU-8"
    BUNDLE_ID com.subverselab.ru8
    VERSION "1.0.0"
    FORMATS ${SVL_RU8_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE   # a host keeps its keys (rule 4); the standalone's editor asks for focus itself
    COPY_PLUGIN_AFTER_BUILD ${SVL_RU8_COPY_AFTER_BUILD}
    VST3_CATEGORIES Instrument Drum
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
)

juce_generate_juce_header(RU8)

# Panel typefaces (TeX Gyre Heros, DejaVu Sans Mono) and the SubverseLab marks, compiled in.
juce_add_binary_data(RU8Assets SOURCES
    Assets/mark_cream.png Assets/wordmark_cream.png
    Assets/Fonts/texgyreheros-bold.otf Assets/Fonts/texgyreheros-regular.otf
    Assets/Fonts/texgyreheros-italic.otf Assets/Fonts/DejaVuSansMono-Bold.ttf)
set_target_properties(RU8Assets PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(RU8_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/UiCommon.cpp
    Source/ui/Widgets.cpp
    Source/ui/Water.cpp
    Source/ui/PadBed.cpp
    Source/ui/Readout.cpp
    Source/ui/Front.cpp
    Source/ui/Rear.cpp
    Source/ui/Help.cpp
    Source/ui/Presets.cpp
)
target_sources(RU8 PRIVATE ${RU8_SOURCES})

set(RU8_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_compile_definitions(RU8 PUBLIC ${RU8_DEFS})

target_link_libraries(RU8
    PRIVATE
        RU8Assets
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Console tools built against the same sources (offline, no host).
set(RU8_TOOL_DEFS ${RU8_DEFS}
    JucePlugin_Name="RU-8" JucePlugin_VersionString="${PROJECT_VERSION}"
    JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1
    JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0)

function(ru8_tool name)
    juce_add_console_app(${name} PRODUCT_NAME "${name}")
    juce_generate_juce_header(${name})
    target_sources(${name} PRIVATE tests/${name}.cpp ${RU8_SOURCES})
    target_compile_definitions(${name} PRIVATE ${RU8_TOOL_DEFS} JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(${name} PRIVATE
        RU8Assets
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_audio_formats
        juce::juce_gui_basics juce::juce_graphics juce::juce_core
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endfunction()

# plugin_check: the processor against the raw engine, timing in a simulated host, state, MIDI (offline).
ru8_tool(plugin_check)
# ui_snapshot: draws the editor to PNGs without a screen, and runs the panel checks (glide, double-click, help, lockup).
ru8_tool(ui_snapshot)
# controlproof: SvlControlProof against the processor (every parameter, blocks, sample rates, state, corners, CPU).
ru8_tool(controlproof)
# rt_check: no heap allocation inside processBlock while playing, and the CPU share at 48 kHz / 512 and 96 kHz / 64.
ru8_tool(rt_check)

# host_check: loads the INSTALLED bundles as a DAW does, plays notes and turns knobs. Read-only.
juce_add_console_app(host_check PRODUCT_NAME "host_check")
juce_generate_juce_header(host_check)
target_sources(host_check PRIVATE tests/host_check.cpp)
target_compile_definitions(host_check PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(host_check PRIVATE
    juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_basics juce::juce_core
    juce::juce_recommended_config_flags)
