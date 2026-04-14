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

    if (! installedModels.empty())
        modelCell.displayValue = stemModelMenuLabel (installedModels[0]);
    else
        modelCell.displayValue = "No models";

    deviceCell.displayValue = stemComputeDeviceToString (selectedDevice);
    outputCell.displayValue = "Beside sample";

    addAndMakeVisible (separateBtn);
    addAndMakeVisible (browseBtn);
    addAndMakeVisible (cancelBtn);

    for (auto* btn : { &separateBtn, &browseBtn, &cancelBtn })
    {
        btn->setColour (juce::TextButton::buttonColourId, getTheme().surface4);
        btn->setColour (juce::TextButton::textColourOnId, getTheme().text2);
        btn->setColour (juce::TextButton::textColourOffId, getTheme().text2);
    }

    separateBtn.setEnabled (! installedModels.empty());

    separateBtn.onClick = [this]
    {
        if (installedModels.empty())
            return;

        const auto chosenModel = installedModels[(size_t) selectedModelIndex];
        processor.setStemComputeDevice (selectedDevice);

        if (useCustomFolder && customOutputFolder.isDirectory())
            processor.startStemSeparation (targetSampleId, chosenModel, customOutputFolder);
        else
            processor.startStemSeparation (targetSampleId, chosenModel);

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
}

void StemExportPanel::resized()
{
    int h = getHeight();
    int pad = 4;
    int btnH = h - pad * 2;
    int gap = 6;

    int cancelW = 60;
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
        modelCell.displayValue = stemModelMenuLabel (installedModels[(size_t) selectedModelIndex]);
        repaint();
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
