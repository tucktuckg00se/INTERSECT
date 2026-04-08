#include "PluginProcessor.h"

#if JucePlugin_Build_Standalone

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/detail/juce_CreatePluginFilter.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace
{
constexpr auto kStandaloneTempoProperty = "standaloneTransportBpm";
constexpr float kStandaloneTempoDefault = 120.0f;
constexpr float kStandaloneTempoMin = 20.0f;
constexpr float kStandaloneTempoMax = 999.0f;

float clampStandaloneTempo (float bpm)
{
    return juce::jlimit (kStandaloneTempoMin, kStandaloneTempoMax, bpm);
}

IntersectProcessor* getIntersectProcessor (juce::AudioProcessor* processor)
{
    return dynamic_cast<IntersectProcessor*> (processor);
}

class StandaloneSettingsComponent final : public juce::Component
{
public:
    StandaloneSettingsComponent (juce::StandalonePluginHolder& pluginHolderIn,
                                 juce::AudioDeviceManager& deviceManagerToUse,
                                 int maxAudioInputChannels,
                                 int maxAudioOutputChannels)
        : pluginHolder (pluginHolderIn),
          deviceSelector (deviceManagerToUse,
                          0, maxAudioInputChannels,
                          0, maxAudioOutputChannels,
                          true,
                          (pluginHolder.processor.get() != nullptr && pluginHolder.processor->producesMidi()),
                          true, false)
    {
        setOpaque (true);

        bpmLabel.setText ("Transport BPM", juce::dontSendNotification);
        bpmLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (bpmLabel);

        bpmSlider.setRange (kStandaloneTempoMin, kStandaloneTempoMax, 0.01);
        bpmSlider.setSliderStyle (juce::Slider::LinearBar);
        bpmSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 90, 24);
        bpmSlider.setNumDecimalPlacesToDisplay (2);
        bpmSlider.onValueChange = [this]
        {
            applyTempoToProcessor ((float) bpmSlider.getValue());
        };
        addAndMakeVisible (bpmSlider);

        addAndMakeVisible (deviceSelector);

        if (pluginHolder.getProcessorHasPotentialFeedbackLoop())
        {
            shouldMuteButton.setButtonText ("Mute audio input");
            shouldMuteButton.setClickingTogglesState (true);
            shouldMuteButton.getToggleStateValue().referTo (pluginHolder.getMuteInputValue());
            shouldMuteLabel.setText ("Feedback Loop:", juce::dontSendNotification);
            shouldMuteLabel.attachToComponent (&shouldMuteButton, true);
            addAndMakeVisible (shouldMuteButton);
            addAndMakeVisible (shouldMuteLabel);
        }

        if (auto* processor = getIntersectProcessor (pluginHolder.processor.get()))
            bpmSlider.setValue (processor->getStandaloneTransportBpm(), juce::dontSendNotification);
        else
            bpmSlider.setValue (kStandaloneTempoDefault, juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
    }

    void resized() override
    {
        const juce::ScopedValueSetter<bool> scope (isResizing, true);
        auto bounds = getLocalBounds().reduced (12);

        if (pluginHolder.getProcessorHasPotentialFeedbackLoop())
        {
            const auto itemHeight = deviceSelector.getItemHeight();
            auto feedbackRow = bounds.removeFromTop (itemHeight + (itemHeight / 2));
            shouldMuteButton.setBounds (juce::Rectangle<int> (feedbackRow.proportionOfWidth (0.35f),
                                                              itemHeight / 2,
                                                              feedbackRow.proportionOfWidth (0.60f),
                                                              itemHeight));
        }

        auto tempoRow = bounds.removeFromTop (32);
        bpmLabel.setBounds (tempoRow.removeFromLeft (130));
        bpmSlider.setBounds (tempoRow);
        bounds.removeFromTop (12);
        deviceSelector.setBounds (bounds);
    }

    void childBoundsChanged (juce::Component* childComp) override
    {
        if (! isResizing && childComp == &deviceSelector)
            setToRecommendedSize();
    }

    void setToRecommendedSize()
    {
        const int feedbackHeight = pluginHolder.getProcessorHasPotentialFeedbackLoop()
            ? deviceSelector.getItemHeight() + (deviceSelector.getItemHeight() / 2)
            : 0;
        setSize (560, deviceSelector.getHeight() + feedbackHeight + 56);
    }

private:
    void applyTempoToProcessor (float bpm)
    {
        const float clamped = clampStandaloneTempo (bpm);

        if (auto* settings = pluginHolder.settings.get())
            settings->setValue (kStandaloneTempoProperty, clamped);

        if (auto* processor = getIntersectProcessor (pluginHolder.processor.get()))
            processor->setStandaloneTransportBpm (clamped);
    }

    juce::StandalonePluginHolder& pluginHolder;
    juce::AudioDeviceSelectorComponent deviceSelector;
    juce::Label bpmLabel;
    juce::Slider bpmSlider;
    juce::Label shouldMuteLabel;
    juce::ToggleButton shouldMuteButton;
    bool isResizing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StandaloneSettingsComponent)
};

class IntersectStandaloneWindow final : public juce::DocumentWindow,
                                        private juce::Button::Listener
{
public:
    explicit IntersectStandaloneWindow (const juce::String& title,
                                        std::unique_ptr<juce::StandalonePluginHolder> pluginHolderIn)
        : juce::DocumentWindow (title,
                                juce::LookAndFeel::getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
                                juce::DocumentWindow::minimiseButton | juce::DocumentWindow::closeButton),
          pluginHolder (std::move (pluginHolderIn)),
          optionsButton ("Options")
    {
        setConstrainer (&decoratorConstrainer);
        setTitleBarButtonsRequired (juce::DocumentWindow::minimiseButton | juce::DocumentWindow::closeButton, false);
        addAndMakeVisible (optionsButton);
        optionsButton.addListener (this);
        optionsButton.setTriggeredOnMouseDown (true);

        applyStandaloneTempoSetting();
        updateContent();

        bool restoredWindowPosition = false;
        if (auto* props = pluginHolder->settings.get())
        {
            constexpr int defaultValue = -100;
            const int x = props->getIntValue ("windowX", defaultValue);
            const int y = props->getIntValue ("windowY", defaultValue);

            if (x != defaultValue && y != defaultValue)
            {
                setTopLeftPosition (x, y);
                restoredWindowPosition = true;
            }
        }

        if (auto* processor = getAudioProcessor())
            if (auto* editor = processor->getActiveEditor())
                setResizable (editor->isResizable(), false);

        if (! restoredWindowPosition)
            centreWithSize (getWidth(), getHeight());
    }

    ~IntersectStandaloneWindow() override
    {
        if (auto* props = pluginHolder->settings.get())
        {
            props->setValue ("windowX", getX());
            props->setValue ("windowY", getY());
        }

        pluginHolder->stopPlaying();
        clearContentComponent();
        pluginHolder = nullptr;
    }

    juce::AudioProcessor* getAudioProcessor() const noexcept
    {
        return pluginHolder->processor.get();
    }

    void showAudioSettingsDialog()
    {
        juce::DialogWindow::LaunchOptions options;

        int maxNumInputs = 0;
        int maxNumOutputs = 0;

        if (pluginHolder->channelConfiguration.size() > 0)
        {
            auto& defaultConfig = pluginHolder->channelConfiguration.getReference (0);
            maxNumInputs = juce::jmax (0, (int) defaultConfig.numIns);
            maxNumOutputs = juce::jmax (0, (int) defaultConfig.numOuts);
        }

        if (auto* bus = pluginHolder->processor->getBus (true, 0))
            maxNumInputs = juce::jmax (0, bus->getDefaultLayout().size());

        if (auto* bus = pluginHolder->processor->getBus (false, 0))
            maxNumOutputs = juce::jmax (0, bus->getDefaultLayout().size());

        auto content = std::make_unique<StandaloneSettingsComponent> (*pluginHolder,
                                                                      pluginHolder->deviceManager,
                                                                      maxNumInputs,
                                                                      maxNumOutputs);
        content->setSize (560, 600);
        content->setToRecommendedSize();

        options.content.setOwned (content.release());
        options.dialogTitle = "Audio/MIDI Settings";
        options.dialogBackgroundColour = options.content->getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.launchAsync();
    }

    void resetToDefaultState()
    {
        pluginHolder->stopPlaying();
        clearContentComponent();
        pluginHolder->deletePlugin();

        if (auto* props = pluginHolder->settings.get())
            props->removeValue ("filterState");

        pluginHolder->createPlugin();
        applyStandaloneTempoSetting();
        updateContent();
        pluginHolder->startPlaying();
    }

    void closeButtonPressed() override
    {
        pluginHolder->savePluginState();
        juce::JUCEApplicationBase::quit();
    }

    void resized() override
    {
        juce::DocumentWindow::resized();
        optionsButton.setBounds (8, 6, 60, getTitleBarHeight() - 8);
    }

private:
    class MainContentComponent final : public juce::Component,
                                       private juce::Value::Listener,
                                       private juce::Button::Listener,
                                       private juce::ComponentListener
    {
    public:
        explicit MainContentComponent (IntersectStandaloneWindow& ownerIn)
            : owner (ownerIn),
              notification (this),
              editor (owner.getAudioProcessor()->hasEditor()
                          ? owner.getAudioProcessor()->createEditorIfNeeded()
                          : new juce::GenericAudioProcessorEditor (*owner.getAudioProcessor()))
        {
            inputMutedValue.referTo (owner.pluginHolder->getMuteInputValue());

            if (editor != nullptr)
            {
                editor->addComponentListener (this);
                handleMovedOrResized();
                addAndMakeVisible (editor.get());
            }

            addChildComponent (notification);

            if (owner.pluginHolder->getProcessorHasPotentialFeedbackLoop())
            {
                inputMutedValue.addListener (this);
                shouldShowNotification = inputMutedValue.getValue();
            }

            inputMutedChanged (shouldShowNotification);
        }

        ~MainContentComponent() override
        {
            if (editor != nullptr)
            {
                editor->removeComponentListener (this);
                owner.pluginHolder->processor->editorBeingDeleted (editor.get());
                editor = nullptr;
            }
        }

        void resized() override
        {
            auto bounds = getLocalBounds();

            if (shouldShowNotification)
                notification.setBounds (bounds.removeFromTop (NotificationArea::height));

            if (editor != nullptr)
            {
                const auto newPos = bounds.getTopLeft().toFloat().transformedBy (editor->getTransform().inverted());

                if (preventResizingEditor)
                    editor->setTopLeftPosition (newPos.roundToInt());
                else
                    editor->setBoundsConstrained (editor->getLocalArea (this, bounds.toFloat()).withPosition (newPos).toNearestInt());
            }
        }

        juce::ComponentBoundsConstrainer* getEditorConstrainer() const
        {
            return editor != nullptr ? editor->getConstrainer() : nullptr;
        }

        juce::BorderSize<int> computeBorder() const
        {
            const auto nativeFrame = [&]() -> juce::BorderSize<int>
            {
                if (auto* peer = owner.getPeer())
                    if (const auto frameSize = peer->getFrameSizeIfPresent())
                        return *frameSize;

                return {};
            }();

            return nativeFrame.addedTo (owner.getContentComponentBorder())
                              .addedTo (juce::BorderSize<int> { shouldShowNotification ? NotificationArea::height : 0, 0, 0, 0 });
        }

    private:
        class NotificationArea final : public juce::Component
        {
        public:
            enum { height = 30 };

            explicit NotificationArea (juce::Button::Listener* listener)
                : notification ("notification", "Audio input is muted to avoid feedback loop"),
                  settingsButton ("Settings...")
            {
                setOpaque (true);
                notification.setColour (juce::Label::textColourId, juce::Colours::black);
                settingsButton.addListener (listener);
                addAndMakeVisible (notification);
                addAndMakeVisible (settingsButton);
            }

            void paint (juce::Graphics& g) override
            {
                auto bounds = getLocalBounds();
                g.setColour (juce::Colours::darkgoldenrod);
                g.fillRect (bounds.removeFromBottom (1));
                g.setColour (juce::Colours::lightgoldenrodyellow);
                g.fillRect (bounds);
            }

            void resized() override
            {
                auto bounds = getLocalBounds().reduced (5);
                settingsButton.setBounds (bounds.removeFromRight (70));
                notification.setBounds (bounds);
            }

        private:
            juce::Label notification;
            juce::TextButton settingsButton;
        };

        void inputMutedChanged (bool newInputMutedValue)
        {
            shouldShowNotification = newInputMutedValue;
            notification.setVisible (shouldShowNotification);

            if (editor != nullptr)
            {
                const int extraHeight = shouldShowNotification ? NotificationArea::height : 0;
                const auto rect = getLocalArea (editor.get(), editor->getLocalBounds());
                setSize (rect.getWidth(), rect.getHeight() + extraHeight);
            }
        }

        void valueChanged (juce::Value& value) override
        {
            inputMutedChanged (value.getValue());
        }

        void buttonClicked (juce::Button*) override
        {
            owner.showAudioSettingsDialog();
        }

        void componentMovedOrResized (juce::Component&, bool, bool) override
        {
            handleMovedOrResized();
        }

        void handleMovedOrResized()
        {
            const juce::ScopedValueSetter<bool> scope (preventResizingEditor, true);

            if (editor != nullptr)
            {
                auto rect = getLocalArea (editor.get(), editor->getLocalBounds());
                setSize (rect.getWidth(),
                         rect.getHeight() + (shouldShowNotification ? NotificationArea::height : 0));
            }
        }

        IntersectStandaloneWindow& owner;
        NotificationArea notification;
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        juce::Value inputMutedValue;
        bool shouldShowNotification = false;
        bool preventResizingEditor = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainContentComponent)
    };

    class DecoratorConstrainer final : public juce::BorderedComponentBoundsConstrainer
    {
    public:
        juce::ComponentBoundsConstrainer* getWrappedConstrainer() const override
        {
            return contentComponent != nullptr ? contentComponent->getEditorConstrainer() : nullptr;
        }

        juce::BorderSize<int> getAdditionalBorder() const override
        {
            return contentComponent != nullptr ? contentComponent->computeBorder() : juce::BorderSize<int> {};
        }

        void setMainContentComponent (MainContentComponent* in)
        {
            contentComponent = in;
        }

    private:
        MainContentComponent* contentComponent = nullptr;
    };

    void updateContent()
    {
        auto* content = new MainContentComponent (*this);
        decoratorConstrainer.setMainContentComponent (content);
        setContentOwned (content, true);
    }

    void handleMenuResult (int result)
    {
        switch (result)
        {
            case 1: showAudioSettingsDialog(); break;
            case 2: pluginHolder->askUserToSaveState(); break;
            case 3: pluginHolder->askUserToLoadState(); break;
            case 4: resetToDefaultState(); break;
            default: break;
        }
    }

    void applyStandaloneTempoSetting()
    {
        float tempo = kStandaloneTempoDefault;

        if (auto* props = pluginHolder->settings.get())
            tempo = (float) props->getDoubleValue (kStandaloneTempoProperty, kStandaloneTempoDefault);

        if (auto* processor = getIntersectProcessor (pluginHolder->processor.get()))
            processor->setStandaloneTransportBpm (tempo);
    }

    void buttonClicked (juce::Button* button) override
    {
        juce::ignoreUnused (button);
        juce::PopupMenu menu;
        menu.addItem (1, "Audio/MIDI Settings...");
        menu.addSeparator();
        menu.addItem (2, "Save current state...");
        menu.addItem (3, "Load a saved state...");
        menu.addSeparator();
        menu.addItem (4, "Reset to default state");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (button),
                            juce::ModalCallbackFunction::create ([this] (int result)
                            {
                                handleMenuResult (result);
                            }));
    }

    std::unique_ptr<juce::StandalonePluginHolder> pluginHolder;
    juce::TextButton optionsButton;
    DecoratorConstrainer decoratorConstrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IntersectStandaloneWindow)
};

class IntersectStandaloneApp final : public juce::JUCEApplication
{
public:
    IntersectStandaloneApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = JucePlugin_Name;
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName = "~/.config";
       #else
        options.folderName = "";
       #endif
        appProperties.setStorageParameters (options);
    }

    const juce::String getApplicationName() override { return JucePlugin_Name; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted (const juce::String&) override {}

    void initialise (const juce::String&) override
    {
        mainWindow = std::make_unique<IntersectStandaloneWindow> (getApplicationName(), createPluginHolder());
        mainWindow->setVisible (true);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            mainWindow->closeButtonPressed();
        else
            quit();
    }

private:
    std::unique_ptr<juce::StandalonePluginHolder> createPluginHolder()
    {
       #ifdef JucePlugin_PreferredChannelConfigurations
        constexpr juce::StandalonePluginHolder::PluginInOuts channels[] { JucePlugin_PreferredChannelConfigurations };
        const juce::Array<juce::StandalonePluginHolder::PluginInOuts> channelConfig (channels, juce::numElementsInArray (channels));
       #else
        const juce::Array<juce::StandalonePluginHolder::PluginInOuts> channelConfig;
       #endif

        return std::make_unique<juce::StandalonePluginHolder> (appProperties.getUserSettings(),
                                                               false,
                                                               juce::String {},
                                                               nullptr,
                                                               channelConfig,
                                                               false);
    }

    juce::ApplicationProperties appProperties;
    std::unique_ptr<IntersectStandaloneWindow> mainWindow;
};
} // namespace

juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new IntersectStandaloneApp();
}

#endif
