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
    outputCell.label = "OUTPUT";

    deviceCell.displayValue = stemComputeDeviceToString (selectedDevice);
    outputCell.displayValue = "Beside sample";
    updateSelectedModelDisplay();
    rebuildStemToggles();

    addAndMakeVisible (separateBtn);
    addAndMakeVisible (browseBtn);
    addAndMakeVisible (cancelBtn);

    for (auto* btn : { &separateBtn, &browseBtn, &cancelBtn })
    {
        btn->setColour (juce::TextButton::buttonColourId, getTheme().surface4);
        btn->setColour (juce::TextButton::textColourOnId, getTheme().text2);
        btn->setColour (juce::TextButton::textColourOffId, getTheme().text2);
    }

    separateBtn.onClick = [this]
    {
        if (installedModels.empty())
            return;

        const auto chosenModel = installedModels[(size_t) selectedModelIndex];
        const auto selectionMask = getStemSelectionMask();
        processor.setStemComputeDevice (selectedDevice);

        if (useCustomFolder && customOutputFolder.isDirectory())
            processor.startStemSeparation (targetSampleId, chosenModel, selectionMask, customOutputFolder);
        else
            processor.startStemSeparation (targetSampleId, chosenModel, selectionMask);

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
    updateSeparateButtonState();
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
    drawCell (outputCell);

    if (! availableRoles.empty())
    {
        auto toggleArea = getLocalBounds().withTrimmedTop (38).reduced (4, 2);
        g.setFont (IntersectLookAndFeel::makeFont (9.0f, true));
        g.setColour (getTheme().text2.withAlpha (0.6f));
        g.drawText ("STEMS", toggleArea.removeFromTop (12), juce::Justification::centredLeft);
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

    int separateW = 76;
    int browseW = 24;
    int rightEdge = cancelBtn.getX() - gap;
    int outputW = rightEdge - separateW - gap - browseW - gap - x;
    outputW = juce::jmax (60, outputW);

    outputCell.bounds = { x, pad, outputW, btnH };
    x += outputW + gap;

    browseBtn.setBounds (x, pad, browseW, btnH);
    x += browseW + gap;

    separateBtn.setBounds (x, pad, separateW, btnH);

    auto toggleArea = getLocalBounds().withTrimmedTop (topRowH + pad * 2 + 6).reduced (pad, 0);
    const int toggleW = 88;
    const int toggleH = 20;
    const int toggleGap = 8;
    int toggleX = toggleArea.getX();
    int toggleY = toggleArea.getY();

    for (auto& button : stemToggleButtons)
    {
        if (toggleX + toggleW > toggleArea.getRight())
        {
            toggleX = toggleArea.getX();
            toggleY += toggleH + 4;
        }

        button->setBounds (toggleX, toggleY, toggleW, toggleH);
        toggleX += toggleW + toggleGap;
    }
}

int StemExportPanel::hitTestCell (juce::Point<int> pos) const
{
    if (modelCell.bounds.contains (pos))  return 0;
    if (deviceCell.bounds.contains (pos)) return 1;
    if (outputCell.bounds.contains (pos)) return 2;
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
        if (useCustomFolder)
        {
            useCustomFolder = false;
            customOutputFolder = juce::File();
            outputCell.displayValue = "Beside sample";
            repaint();
        }
    }
}

void StemExportPanel::rebuildStemToggles()
{
    for (auto& button : stemToggleButtons)
        removeChildComponent (button.get());

    stemToggleButtons.clear();
    availableRoles.clear();

    if (! installedModels.empty())
    {
        const auto entry = getEffectiveStemModelCatalogEntry (installedModels[(size_t) selectedModelIndex],
                                                              processor.getResolvedStemModelFolder());
        for (int i = 0; i < entry.numModelOutputs; ++i)
        {
            const auto role = entry.modelOutputRoles[i];
            availableRoles.push_back (role);

            auto button = std::make_unique<juce::ToggleButton> (stemRoleToString (role).toUpperCase());
            button->setClickingTogglesState (true);
            button->setToggleState (false, juce::dontSendNotification);
            button->setColour (juce::ToggleButton::textColourId, getTheme().text1);
            button->onClick = [this] { updateSeparateButtonState(); };
            addAndMakeVisible (*button);
            stemToggleButtons.push_back (std::move (button));
        }
    }

    updateSeparateButtonState();
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

void StemExportPanel::updateSeparateButtonState()
{
    separateBtn.setEnabled (! installedModels.empty() && getStemSelectionMask() != 0);
}

StemSelectionMask StemExportPanel::getStemSelectionMask() const
{
    StemSelectionMask mask = 0;
    for (size_t i = 0; i < stemToggleButtons.size(); ++i)
        if (stemToggleButtons[i]->getToggleState())
            mask |= stemSelectionBitForIndex ((int) i);
    return mask;
}
