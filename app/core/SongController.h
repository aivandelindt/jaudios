#pragma once

#include "AudioEngine.h"
#include "ExportService.h"
#include "SongPipeline.h"

#include "engine/export/Export.h"

#include <juce_data_structures/juce_data_structures.h>

namespace jaudios
{

/** A WAV export tile's render, running on the controller's export thread (the app keeps playing
    and stays responsive). State and progress are atomics the UI polls. */
class WavExportJob
{
public:
    enum class State { rendering, done, failed, cancelled };
    State state() const noexcept { return status.load(); }
    double progress() const noexcept { return fraction.load(); }
    void cancel() noexcept { cancelRequested.store (true); }
    /** Valid once done. */
    const juce::File& file() const noexcept { return result; }
    const juce::String& partName() const noexcept { return part; }
    /** The song's revision it renders (SongController::revision()): stale when the song changed. */
    int revision() const noexcept { return songRevision; }

private:
    friend class SongController;
    std::atomic<State> status { State::rendering };
    std::atomic<double> fraction { 0.0 };
    std::atomic<bool> cancelRequested { false };
    std::unique_ptr<AudioEngine> engine; // set up on the message thread, rendered on the export thread
    juce::File result;
    juce::String part;
    int songRevision = 0;
};

/** The song the app is editing (ui-shell): the analysed source, the chosen progression with the
    user's chord replacements, the style and the mixer, kept in a ValueTree with an UndoManager.
    Every change, made by an action or by undo / redo, reaches the engine through the tree's
    listener: new clips for the generated channels, mixer settings for the strips. The UI only
    reads the controller and calls its actions. Message thread. */
class SongController final : private juce::ValueTree::Listener, private juce::AudioProcessorValueTreeState::Listener
{
public:
    SongController (AudioEngine&, std::shared_ptr<const SongAnalysis>, std::shared_ptr<const juce::AudioBuffer<float>> source,
                    generators::Style);
    ~SongController() override;

    const SongAnalysis& song() const noexcept { return *analysed; }
    const SongParts& parts() const noexcept { return current; }
    const exporting::BarGrid& grid() const noexcept { return barGrid; }
    double durationSeconds() const noexcept { return analysed->durationSec; }
    juce::ValueTree& state() noexcept { return tree; }
    juce::UndoManager& undoManager() noexcept { return undo; }
    void setSongName (const juce::String& n) { name = n; }   // export file names
    juce::String songName() const { return name.isNotEmpty() ? name : juce::String ("Song"); }
    AudioEngine& audioEngine() noexcept { return engine; }

    // --- actions (each one undoable step) -----------------------------------------------
    generators::Style style() const;
    void setStyle (generators::Style);                  // new suggestions; drops the chord replacements
    int progressionIndex() const;
    int numProgressions() const noexcept { return (int) analysed->progressions.size(); }
    const generators::Progression& suggestion (int index) const { return analysed->progressions[(size_t) index]; }
    void selectProgression (int index);                 // drops the chord replacements
    /** The progression playing: the selected suggestion with the user's replacements. */
    const generators::Progression& currentProgression() const noexcept { return current.progression; }
    std::vector<generators::Alternative> alternativesFor (int slot) const;
    void replaceChord (int slot, const theory::Chord&);
    void resetChord (int slot);
    bool isReplaced (int slot) const;

    // --- accent editor (bass and drum lanes) ----------------------------------------------
    /** A drum hit's place: its element, bar and 1/16 step in the bar (the generator's grid). */
    struct DrumHit
    {
        generators::DrumElement element = generators::DrumElement::kick;
        int bar = 0, step = 0;
    };
    /** The hit's velocity after the user's edits (0 = no hit there), and before them. */
    int drumVelocity (DrumHit) const;
    int generatedDrumVelocity (DrumHit) const;
    bool isDrumHitEdited (DrumHit) const;
    /** 0 removes the hit; on a step the groove leaves empty, a velocity adds one. A run of calls
        for the same hit (a drag) is one undo step. */
    void setDrumVelocity (DrumHit, int velocity);
    /** A click: normal -> accent -> ghost -> off -> normal; on an empty step it adds a hit. */
    void cycleDrumAccent (DrumHit);
    void resetDrumHit (DrumHit);

    /** Bass notes are addressed by their 1/16 step on the export grid (it survives chord changes:
        the rhythm stays, the pitches follow the chords). */
    int bassStepOf (const generators::BassNote&) const;
    /** The parts before the accent edits (the lanes draw switched-off notes from these). */
    const std::vector<generators::DrumNote>& generatedDrumNotes() const noexcept { return generatedDrums; }
    const std::vector<generators::BassNote>& generatedBassNotes() const noexcept { return generatedBass; }
    int bassVelocity (int step) const;               // 0 = no note there (or switched off)
    int generatedBassVelocity (int step) const;
    bool isBassNoteEdited (int step) const;
    void setBassVelocity (int step, int velocity);   // 0 = off; only on the generated notes' steps
    void cycleBassAccent (int step);
    void resetBassNote (int step);
    /** Drops every accent edit (one undo step). */
    void clearAccents();
    int numAccentEdits() const;

    /** Accent levels a click cycles through, from the generated velocity. */
    static int accentOf (int velocity) noexcept { return juce::jlimit (1, 127, juce::roundToInt (velocity * 1.3) + 8); }
    static int ghostOf (int velocity) noexcept { return juce::jlimit (1, 127, juce::roundToInt (velocity * 0.4)); }
    static constexpr int addedHitVelocity = 96;

    float gainDecibels (ChannelId) const;
    bool isMuted (ChannelId) const;
    bool isSoloed (ChannelId) const;
    void setGainDecibels (ChannelId, float dB);
    void setMute (ChannelId, bool);
    void setSolo (ChannelId, bool);

    // --- export tiles ---------------------------------------------------------------------
    /** A part as a file in `dir` for dragging into a DAW: "chords" / "bass" / "drums" / "all" as
        type-1 MIDI, or any export part (mix, chords, stems...) as a 24-bit WAV with a tail, rendered
        offline through a second engine set up like this one (same instruments, FX and mixer). */
    juce::File exportMidi (const juce::String& part, const juce::File& dir) const;
    /** Renders and returns the WAV now (blocking: tests, tools). */
    juce::File exportWav (const juce::String& part, const juce::File& dir, double tailSeconds = 2.0);
    /** Starts rendering on the export thread; null when `part` is not exportable. The engine is
        set up here (message thread) from the current state, so later edits don't affect it. */
    std::shared_ptr<WavExportJob> startWavExport (const juce::String& part, const juce::File& dir, double tailSeconds = 2.0);
    /** Bumped by every change that alters what an export would contain: parts, accents, mixer, FX. */
    int revision() const noexcept { return rev.load(); }

    std::function<void()> onPartsChanged;               // the lanes repaint
    std::function<void()> onMixerChanged;               // the strips update

    static constexpr int channelCount = numChannelIds;

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void parameterChanged (const juce::String&, float) override { ++rev; } // an FX parameter (any strip)
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void rebuildParts();
    void applyAccentEdits();
    juce::ValueTree editsNode() const;
    juce::ValueTree drumEdit (DrumHit) const;
    juce::ValueTree bassEdit (int step) const;
    void setEdit (juce::ValueTree existing, std::function<juce::ValueTree()> make, int velocity, const juce::String& undoName, bool coalesce);
    void writeDrum (DrumHit, int velocity, bool coalesce);
    void writeBass (int step, int velocity, bool coalesce);
    void applyMixer();
    juce::ValueTree channelNode (ChannelId) const;
    juce::ValueTree chordsNode() const;
    void setInstrumentsForStyle();

    AudioEngine& engine;
    std::shared_ptr<SongAnalysis> analysed;
    std::shared_ptr<const juce::AudioBuffer<float>> sourceBuffer;
    exporting::BarGrid barGrid;
    SongParts current;
    std::vector<generators::DrumNote> generatedDrums;   // before the accent edits
    std::vector<generators::BassNote> generatedBass;
    generators::Style suggestedFor;
    std::string chordsPreset, bassPreset;
    juce::String name;
    juce::ValueTree tree;
    juce::UndoManager undo;
    bool applying = false;
    std::atomic<int> rev { 0 };
    juce::ThreadPool exportThread { 1 };
    std::vector<std::shared_ptr<WavExportJob>> jobs; // kept until the next start or the end (engines die on this thread)
};

/** The default built-in instruments of a style (CLI and app agree). */
std::string defaultChordsPreset (generators::Style);
std::string defaultBassPreset (generators::Style);

} // namespace jaudios
