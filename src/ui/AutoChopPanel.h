#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../audio/AudioAnalysis.h"
#include <memory>
#include <atomic>

class IntersectProcessor;
class WaveformView;

class AutoChopPanel : public juce::Component,
                      private juce::Timer
{
public:
    AutoChopPanel (IntersectProcessor& p, WaveformView& wv);
    ~AutoChopPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

private:
    struct ParamCell
    {
        juce::Rectangle<int> bounds;
        juce::String label;
        float value;
        float minVal, maxVal;
        float step;
        juce::String suffix;
    };

    void updatePreview();
    void updatePreviewFromCachedODF();
    void startODFComputation();
    int hitTestCell (juce::Point<int> pos) const;
    void showTextEditor (ParamCell& cell);
    void dismissTextEditor();

    void timerCallback() override;

    IntersectProcessor& processor;
    WaveformView& waveformView;

    ParamCell sensCell;
    ParamCell minCell;
    ParamCell divCell;

    juce::TextButton splitEqualBtn { "SPLIT EQUAL" };
    juce::TextButton detectBtn     { "SPLIT TRANSIENTS" };
    juce::TextButton cancelBtn     { "CANCEL" };

    int activeDragCell = -1;
    int dragStartY = 0;
    float dragStartValue = 0.0f;

    std::unique_ptr<juce::TextEditor> textEditor;

    // --- ODF cache for async computation ---
    AudioAnalysis::ODFResult cachedODF;
    int cachedSliceStart = -1;
    int cachedSliceEnd   = -1;
    bool odfReady = false;

    // Background thread for initial ODF computation
    class ODFThread : public juce::Thread
    {
    public:
        ODFThread (const juce::AudioBuffer<float>& buf, int start, int end, double sr)
            : juce::Thread ("ODF-Compute"), buffer (buf), sliceStart (start), sliceEnd (end), sampleRate (sr) {}

        void run() override
        {
            result = AudioAnalysis::computeSpectralFluxODF (buffer, sliceStart, sliceEnd, sampleRate);
        }

        AudioAnalysis::ODFResult result;

    private:
        const juce::AudioBuffer<float>& buffer;
        int sliceStart, sliceEnd;
        double sampleRate;
    };

    std::unique_ptr<ODFThread> odfThread;
    std::shared_ptr<juce::AudioBuffer<float>> odfBufferSnapshot; // prevent dangling ref

    bool debounceScheduled = false;
};
