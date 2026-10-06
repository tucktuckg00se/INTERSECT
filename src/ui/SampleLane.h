#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <memory>

class IntersectProcessor;
class StemExportPanel;
class WaveformView;

class SampleLane : public juce::Component,
                   public juce::DragAndDropTarget,
                   public juce::TooltipClient
{
public:
    SampleLane (IntersectProcessor& p, WaveformView& wv);
    ~SampleLane() override;
    std::function<void()> onInteraction;
    /** Files dragged here from the browser. */
    std::function<void (const std::vector<juce::File>&)> onFilesDropped;
    /** STEMS was clicked; the panel opens over the waveform, so the browser must close. */
    std::function<void()> onStemPanelRequested;
    /** MIDI dragged out of a sample: write that sample's slices to a .mid and return it ({} if none). */
    std::function<juce::File (int sampleId)> writeMidiForDrag;
    /** MIDI clicked on a sample: save that sample's slices as a .mid via a file chooser. */
    std::function<void (int sampleId)> onMidiSaveRequested;

    /** While browsing, an empty lane explains how to add files. */
    void setShowEmptyHint (bool shouldShow);

    bool isInterestedInDragSource (const SourceDetails& details) override;
    void itemDragEnter (const SourceDetails& details) override;
    void itemDragExit (const SourceDetails& details) override;
    void itemDropped (const SourceDetails& details) override;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    juce::String getTooltip() override;

    bool isStemExportOpen() const { return stemExportPanel != nullptr; }

private:
    struct VisibleSample
    {
        int sampleId = 0;
        int index = 0;
        int x1 = 0;
        int x2 = 0;
        bool selected = false;
        juce::Colour colour;
        juce::String label;
        juce::Rectangle<int> midiBounds;
        juce::Rectangle<int> stemsBounds;
        juce::Rectangle<int> deleteBounds;
    };

    std::vector<VisibleSample> buildVisibleSamples() const;
    int hitTestSample (juce::Point<int> pos) const;
    void showStemExportPanel (int sampleId);
    void dismissStemExportPanel();

    IntersectProcessor& processor;
    WaveformView& waveformView;
    std::unique_ptr<StemExportPanel> stemExportPanel;
    int midiPressSampleId = -1;    // MIDI button pressed: click saves, drag exports
    bool midiDragStarted = false;
    int dragSampleId = -1;
    int dragStartX = 0;
    int dragTargetIndex = -1;
    bool dragging = false;
    bool browserDragHover = false;
    bool showEmptyHint = false;
};
