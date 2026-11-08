#pragma once
#include <juce_core/juce_core.h>
#include "../browser/BrowserTypes.h"

/**
    What the settings view needs from the editor: theme, UI scale, note naming, the browser's
    preset and default folders, and saving settings.yaml.

    Processor-level settings (NRPN, stem models, ONNX Runtime) are changed on the processor
    directly; the page then calls persistSettings().
*/
class SettingsHost
{
public:
    virtual ~SettingsHost() = default;

    virtual juce::StringArray getAvailableThemes() = 0;
    virtual void applyTheme (const juce::String& themeName) = 0;

    virtual int getMiddleCOctave() const = 0;
    virtual void setMiddleCOctave (int octave) = 0;

    /** The scale the user chose (0.5-3.0) and the scale actually rendered after fitting the display. */
    virtual float getDesiredUiScale() const = 0;
    virtual float getEffectiveUiScale() const = 0;
    virtual void adjustUiScale (float delta) = 0;

    virtual juce::File getCustomPresetsFolder() const = 0;
    virtual void setCustomPresetsFolder (const juce::File& folder) = 0;

    virtual Browser::DefaultFolderMode getBrowserDefaultFolderMode() const = 0;
    virtual void setBrowserDefaultFolderMode (Browser::DefaultFolderMode mode) = 0;
    virtual juce::File getBrowserDefaultFolder() const = 0;
    virtual void setBrowserDefaultFolder (const juce::File& folder) = 0;

    /** Writes settings.yaml. */
    virtual void persistSettings() = 0;
};
