#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <memory>

class IntersectProcessor;

class SampleLane : public juce::Component,
                   public juce::DragAndDropTarget,
                   public juce::TooltipClient
{
public:
    explicit SampleLane (IntersectProcessor& p);
    ~SampleLane() override;
    std::function<void()> onInteraction;
    /** Files dragged here from the browser. */
    std::function<void (const std::vector<juce::File>&)> onFilesDropped;
    /** STEMS was clicked on a sample with no job running: the editor opens (or closes) the stem view. */
    std::function<void (int sampleId)> onStemViewRequested;
    /** MIDI dragged out of a sample: write that sample's slices to a .mid and return it ({} if none). */
    std::function<juce::File (int sampleId)> writeMidiForDrag;
    /** MIDI clicked on a sample: save that sample's slices as a .mid via a file chooser. */
    std::function<void (int sampleId)> onMidiSaveRequested;
    /** AUDIO dragged out of a sample: the audio file to hand to the drop target ({} if none). */
    std::function<juce::File (int sampleId)> getAudioFileForDrag;
    /** AUDIO clicked on a sample: save a copy of its audio file via a file chooser. */
    std::function<void (int sampleId)> onAudioSaveRequested;
    /** A missing sample's chip was clicked: relink it. */
    std::function<void (int sampleId)> onRelinkRequested;

    /** While browsing, an empty lane explains how to add files. */
    void setShowEmptyHint (bool shouldShow);
    /** Removes a session sample from the kit. Asks first with a small menu at the sample's X
        unless skipConfirm (Shift held). Every remove path (X, Delete key, DEL) comes through here. */
    void requestRemoveSample (int sampleId, bool skipConfirm);

    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDragEnter (const SourceDetails& details) override;
    void itemDragExit (const SourceDetails& details) override;
    void itemDropped (const SourceDetails& details) override;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    juce::String getTooltip() override;

private:
    struct VisibleSample
    {
        int sampleId = 0;
        int index = 0;
        int x1 = 0;
        int x2 = 0;
        bool selected = false;
        bool missing = false;   // placeholder awaiting relink: no AUDIO, MIDI or STEMS
        juce::Colour colour;
        juce::String label;
        juce::Rectangle<int> audioBounds;
        juce::Rectangle<int> midiBounds;
        juce::Rectangle<int> stemsBounds;
        juce::Rectangle<int> deleteBounds;
    };

    /** The export chips: a click saves a file, a drag hands it to the OS. */
    enum class ExportChip { none, audio, midi };
    struct ChipPress
    {
        int sampleId = -1;
        ExportChip chip = ExportChip::none;
        bool dragStarted = false;
    };

    std::vector<VisibleSample> buildVisibleSamples() const;
    int hitTestSample (juce::Point<int> pos) const;
    static juce::Rectangle<int> boundsOf (const VisibleSample& sample, ExportChip chip);
    /** Starts an OS file drag from this component (call from mouseDrag). */
    void startExternalFileDrag (const juce::File& file);

    IntersectProcessor& processor;
    ChipPress chipPress;
    int dragSampleId = -1;
    int dragStartX = 0;
    int dragTargetIndex = -1;
    bool dragging = false;
    bool browserDragHover = false;
    bool showEmptyHint = false;
};
