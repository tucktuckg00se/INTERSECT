#include "StemExportPanel.h"
#include "IntersectLookAndFeel.h"
#include "../PluginProcessor.h"

StemExportPanel::StemExportPanel (IntersectProcessor& p, int sampleId)
    : processor (p), targetSampleId (sampleId)
{
    installedModels = processor.getInstalledStemModels();
    selectedDevice = processor.getStemComputeDevice();

    modelCell.label = "MODEL";
    deviceCell.label = "DEVICE";
    modeCell.label = "MODE";
    outputCell.label = "OUTPUT";

    deviceCell.displayValue = stemComputeDeviceToString (selectedDevice);
    modeCell.displayValue = stemExportModeToString (selectedExportMode);
    outputCell.displayValue = "Beside sample";
    updateSelectedModelDisplay();
    rebuildStemToggles();

    addAndMakeVisible (startBtn);
    addAndMakeVisible (browseBtn);
    addAndMakeVisible (cancelBtn);

    for (auto* btn : { &startBtn, &browseBtn, &cancelBtn })
    {
        btn->setColour (juce::TextButton::buttonColourId, getTheme().surface4);
        btn->setColour (juce::TextButton::textColourOnId, getTheme().text2);
        btn->setColour (juce::TextButton::textColourOffId, getTheme().text2);
    }

    startBtn.onClick = [this]
    {
        if (installedModels.empty())
            return;

        const auto chosenModel = installedModels[(size_t) selectedModelIndex];
        const auto selectionMask = getStemSelectionMask();
        processor.setStemComputeDevice (selectedDevice);

        if (useCustomFolder && customOutputFolder.isDirectory())
            processor.startStemSeparation (targetSampleId, chosenModel, selectionMask,
                                           selectedExportMode, customOutputFolder);
        else
            processor.startStemSeparation (targetSampleId, chosenModel, selectionMask,
                                           selectedExportMode);

        close();
    };

    browseBtn.onClick = [this]
    {
        auto initial = customOutputFolder.isDirectory() ? customOutputFolder : juce::File();
        fileChooser = std::make_unique<juce::FileChooser> ("Choose Stem Export Folder", initial, "*");
        fileChooser->launchAsync (
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this] (const juce::FileChooser& fc)
            {
                const auto folder = fc.getResult();
                if (folder.isDirectory())
                {
                    customOutputFolder = folder;
                    useCustomFolder = true;
                    outputCell.displayValue = folder.getFileName();
                    repaint();
                }
            });
    };

    cancelBtn.onClick = [this] { close(); };
    updateStartButtonState();
}

StemExportPanel::~StemExportPanel() = default;

void StemExportPanel::close()
{
    if (auto* parent = getParentComponent())
        parent->removeChildComponent (this);
}

void StemExportPanel::paint (juce::Graphics& g)
{
    g.setColour (getTheme().surface2.withAlpha (0.95f));
    g.fillRect (getLocalBounds());

    g.setColour (getTheme().surface5);
    g.drawRect (getLocalBounds(), 1);

    auto drawCell = [&] (const OptionCell& cell)
    {
        g.setColour (getTheme().surface3);
        g.fillRoundedRectangle (cell.bounds.toFloat(), 3.0f);

        auto content = cell.bounds.reduced (4, 0);

        g.setFont (IntersectLookAndFeel::makeFont (9.0f, true));
        g.setColour (getTheme().text2.withAlpha (0.6f));
        g.drawText (cell.label, content.getX(), content.getY() + 1,
                    content.getWidth(), 10, juce::Justification::centredLeft);

        g.setFont (IntersectLookAndFeel::makeFont (10.0f));
        g.setColour (getTheme().text1);
        g.drawText (cell.displayValue, content.getX(), content.getY() + 11,
                    content.getWidth(), content.getHeight() - 12, juce::Justification::centredLeft);
    };

    drawCell (modelCell);
    drawCell (deviceCell);
    drawCell (modeCell);
    drawCell (outputCell);

    // Stem toggles
    if (! stemToggles.empty())
    {
        auto toggleArea = getLocalBounds().withTrimmedTop (33).reduced (4, 0);
        g.setFont (IntersectLookAndFeel::makeFont (9.0f, true));
        g.setColour (getTheme().text2.withAlpha (0.6f));
        g.drawText ("STEMS", toggleArea.removeFromTop (10), juce::Justification::centredLeft);

        for (const auto& toggle : stemToggles)
        {
            if (toggle.selected)
            {
                g.setColour (getTheme().accent.withAlpha (0.8f));
                g.fillRoundedRectangle (toggle.bounds.toFloat(), 3.0f);
                g.setFont (IntersectLookAndFeel::makeFont (9.0f, true));
                g.setColour (getTheme().surface1);
            }
            else
            {
                g.setColour (getTheme().surface4);
                g.fillRoundedRectangle (toggle.bounds.toFloat(), 3.0f);
                g.setFont (IntersectLookAndFeel::makeFont (9.0f));
                g.setColour (getTheme().text2);
            }

            g.drawText (toggle.label, toggle.bounds, juce::Justification::centred);
        }
    }
}

void StemExportPanel::resized()
{
    int pad = 4;
    int topRowH = 26;
    int btnH = topRowH;
    int gap = 6;

    int cancelW = 64;
    cancelBtn.setBounds (getWidth() - cancelW - pad, pad, cancelW, btnH);

    int x = pad;

    modelCell.bounds = { x, pad, 130, btnH };
    x += 130 + gap;

    deviceCell.bounds = { x, pad, 48, btnH };
    x += 48 + gap;

    modeCell.bounds = { x, pad, 76, btnH };
    x += 76 + gap;

    int startW = 64;
    int browseW = 24;
    int rightEdge = cancelBtn.getX() - gap;
    int outputW = rightEdge - startW - gap - browseW - gap - x;
    outputW = juce::jmax (90, outputW);

    outputCell.bounds = { x, pad, outputW, btnH };
    x += outputW + gap;

    browseBtn.setBounds (x, pad, browseW, btnH);
    x += browseW + gap;

    startBtn.setBounds (x, pad, startW, btnH);

    // Layout stem toggle cells — align with paint() STEMS label (top 33 + label 10 + gap 3)
    auto toggleArea = getLocalBounds().withTrimmedTop (46).reduced (pad, 0);
    const int toggleW = 64;
    const int toggleH = 18;
    const int toggleGap = 4;
    int toggleX = toggleArea.getX();
    int toggleY = toggleArea.getY();

    for (auto& toggle : stemToggles)
    {
        if (toggleX + toggleW > toggleArea.getRight())
        {
            toggleX = toggleArea.getX();
            toggleY += toggleH + 3;
        }

        toggle.bounds = { toggleX, toggleY, toggleW, toggleH };
        toggleX += toggleW + toggleGap;
    }
}

int StemExportPanel::hitTestCell (juce::Point<int> pos) const
{
    if (modelCell.bounds.contains (pos))  return 0;
    if (deviceCell.bounds.contains (pos)) return 1;
    if (modeCell.bounds.contains (pos))   return 2;
    if (outputCell.bounds.contains (pos)) return 3;
    return -1;
}

int StemExportPanel::hitTestStemToggle (juce::Point<int> pos) const
{
    for (int i = 0; i < (int) stemToggles.size(); ++i)
        if (stemToggles[(size_t) i].bounds.contains (pos))
            return i;
    return -1;
}

void StemExportPanel::mouseDown (const juce::MouseEvent& e)
{
    int idx = hitTestCell (e.getPosition());

    if (idx == 0 && installedModels.size() > 1)
    {
        selectedModelIndex = (selectedModelIndex + 1) % (int) installedModels.size();
        updateSelectedModelDisplay();
        rebuildStemToggles();
    }
    else if (idx == 1)
    {
        selectedDevice = (selectedDevice == StemComputeDevice::cpu)
                             ? StemComputeDevice::gpu
                             : StemComputeDevice::cpu;
        deviceCell.displayValue = stemComputeDeviceToString (selectedDevice);
        repaint();
    }
    else if (idx == 2)
    {
        selectedExportMode = (selectedExportMode == StemExportMode::combine)
                                 ? StemExportMode::separate
                                 : StemExportMode::combine;
        modeCell.displayValue = stemExportModeToString (selectedExportMode);
        repaint();
    }
    else if (idx == 3)
    {
        if (useCustomFolder)
        {
            useCustomFolder = false;
            customOutputFolder = juce::File();
            outputCell.displayValue = "Beside sample";
            repaint();
        }
    }

    int stemIdx = hitTestStemToggle (e.getPosition());
    if (stemIdx >= 0)
    {
        stemToggles[(size_t) stemIdx].selected = ! stemToggles[(size_t) stemIdx].selected;
        updateStartButtonState();
        repaint();
    }
}

void StemExportPanel::rebuildStemToggles()
{
    stemToggles.clear();
    availableRoles.clear();

    if (! installedModels.empty())
    {
        const auto entry = getEffectiveStemModelCatalogEntry (installedModels[(size_t) selectedModelIndex],
                                                              processor.getResolvedStemModelFolder());
        for (int i = 0; i < entry.numModelOutputs; ++i)
        {
            const auto role = entry.modelOutputRoles[i];
            availableRoles.push_back (role);

            StemToggle toggle;
            toggle.label = stemRoleToString (role).toUpperCase();
            toggle.selected = false;
            stemToggles.push_back (std::move (toggle));
        }
    }

    updateStartButtonState();
    resized();
    repaint();
}

void StemExportPanel::updateSelectedModelDisplay()
{
    if (! installedModels.empty())
        modelCell.displayValue = stemModelMenuLabel (installedModels[(size_t) selectedModelIndex]);
    else
        modelCell.displayValue = "No models";
}

void StemExportPanel::updateStartButtonState()
{
    startBtn.setEnabled (! installedModels.empty() && getStemSelectionMask() != 0);
}

StemSelectionMask StemExportPanel::getStemSelectionMask() const
{
    StemSelectionMask mask = 0;
    for (size_t i = 0; i < stemToggles.size(); ++i)
        if (stemToggles[i].selected)
            mask |= stemSelectionBitForIndex ((int) i);
    return mask;
}
