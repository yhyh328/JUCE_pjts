#include "MainComponent.h"
#include "Piano88Keys.h"
#include <cmath>
#include <algorithm>
#include <string>

//==============================================================================
ButtonSynthesizer::ButtonSynthesizer(double sampleRate, double frequencyInHz)
{
	doClickForNextBuffer.set(0);
    prepareSynthesizerState(sampleRate, frequencyInHz);
}

void ButtonSynthesizer::clickButton(float clickPosition)
{   
	juce::ignoreUnused(clickPosition);
	//if (doClickForNextBuffer.compareAndSetBool(1, 0))
 //   {
 //       amplitude = std::sin(juce::MathConstants<float>::pi);
 //   }
    amplitude = 1.0f;
    doClickForNextBuffer.set(1);
}

void ButtonSynthesizer::generateAndAddData(float* outBuffer, int numSamples)
{
    if (doClickForNextBuffer.compareAndSetBool(0, 1))
    {
        exciteInternalBuffer();
    }
    if (delayLine.empty())
    {
        return;
    }
    for (int i = 0; i < numSamples; ++i)
    {
        auto nextPos = (pos + 1) % delayLine.size();
        delayLine[nextPos] = decay * 0.5f * (delayLine[nextPos] + delayLine[pos]);
        outBuffer[i] += delayLine[pos];
        pos = nextPos;
    }
}

void ButtonSynthesizer::prepareSynthesizerState(double sampleRate, double frequencyInHz)
{
    auto delayLineLength = (size_t)std::round(sampleRate / frequencyInHz);
    jassert(delayLineLength > 50);

    delayLine.resize(delayLineLength);
    excitationSample.resize(delayLineLength);

    std::fill(delayLine.begin(), delayLine.end(), 0.0f);
    std::generate(
        excitationSample.begin(),
        excitationSample.end(),
        [] {
            return (juce::Random::getSystemRandom().nextFloat() * 2.0f) - 1.0f;
        }
    );
}

void ButtonSynthesizer::exciteInternalBuffer()
{
    jassert(delayLine.size() == excitationSample.size());
    pos = 0;
    std::transform(
        excitationSample.begin(),
        excitationSample.end(),
        delayLine.begin(),
        [this](float sample) { return amplitude * sample;  }
    );
}

//==============================================================================
ButtonComponent::ButtonComponent(int lengthInPixels, juce::Colour stringColour):
	length(lengthInPixels), colour(stringColour)
{
    setInterceptsMouseClicks(false, false);
    setSize(length, height);
    startTimerHz(60);
}

void ButtonComponent::paint(juce::Graphics& g)
{
    //g.setColour(colour);
    //g.strokePath(generateButtonPath(), juce::PathStrokeType(2.0f));
    
    // 현 모양을 넓적하게 해서 직사각형 모양으로
    //auto bounds = getLocalBounds().toFloat();
    //g.setColour(colour.withAlpha(0.8f));
    //g.fillRect(bounds);
    //g.setColour(juce::Colours::black);
    //g.drawRect(bounds, 2.0f);

    auto bounds = getLocalBounds().toFloat();

    float pressAmount = juce::jlimit(0.0f, 1.0f, amplitude / maxAmplitude);
    auto inset = pressAmount * 3.0f;

    auto r = bounds.reduced(inset);

    g.setColour(colour.darker(pressAmount * 0.3f));
    g.fillRoundedRectangle(r, 6.0f);

    g.setColour(juce::Colours::black);
    g.drawRoundedRectangle(r, 6.0f, 1.5f);
}   

// generateButtonPaht는 String에서나 필요했음
// 여기서는 필요 없음
//juce::Path ButtonComponent::generateButtonPath() const
//{
//    juce::Path p;
//    const float y = (float)height / 2.0f;
//    p.startNewSubPath(0.0f, y);
//    p.quadraticTo((float)length / 2.0f, y + amplitude, (float)length, y);
//    return p;
//}

void ButtonComponent::timerCallback()
{
    amplitude *= 0.99f;
    repaint();
}

void ButtonComponent::clickButton(float pos)
{
    amplitude = maxAmplitude * std::sin(pos * juce::MathConstants<float>::pi);
}

//==============================================================================
MainComponent::MainComponent()
{
    // Make sure you set the size of the component after
    // you add any child components.

    createButtonComponents();

    setSize (800, 600);

    auto* audioDevice = deviceManager.getCurrentAudioDevice();
    auto numInputs  = audioDevice ? audioDevice->getActiveInputChannels().countNumberOfSetBits() : 0;
    auto numOutputs = audioDevice ? audioDevice->getActiveOutputChannels().countNumberOfSetBits() : 2;
    setAudioChannels(numInputs, std::max(2, numOutputs));

    //// Some platforms require permissions to open input channels so request that here
    //if (juce::RuntimePermissions::isRequired (juce::RuntimePermissions::recordAudio)
    //    && ! juce::RuntimePermissions::isGranted (juce::RuntimePermissions::recordAudio))
    //{
    //    juce::RuntimePermissions::request (juce::RuntimePermissions::recordAudio,
    //                                       [&] (bool granted) { setAudioChannels (granted ? 2 : 0, 2); });
    //}
    //else
    //{
    //    // Specify the number of input and output channels that we want to open
    //    setAudioChannels (2, 2);
    //}
}

MainComponent::~MainComponent()
{
    // This shuts down the audio device and clears the audio source.
    shutdownAudio();
}

//==============================================================================
void MainComponent::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    // This function will be called when the audio device is started, or when
    // its settings (i.e. sample rate, block size, etc) are changed.

    // You can use this function to initialise any resources you might need,
    // but be careful - it will be called on the audio thread, not the GUI thread.

    // For more details, see the help for AudioProcessor::prepareToPlay()
    generateButtonSynths(sampleRate);
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    // Your audio-processing code goes here!

    // For more details, see the help for AudioProcessor::getNextAudioBlock()

    // Right now we are not producing any data, in which case we need to clear the buffer
    // (to prevent the output of random noise)
    bufferToFill.clearActiveBufferRegion();
    for (int ch = 0; ch < bufferToFill.buffer->getNumChannels(); ++ch)
    {
        float* channelData = bufferToFill.buffer->getWritePointer(ch, bufferToFill.startSample);
        if (ch == 0)
        {
            for (auto* syn : buttonSynths)
            {
                syn->generateAndAddData(channelData, bufferToFill.numSamples);
            }
        }
        else
        {
            std::memcpy(
                channelData, bufferToFill.buffer->getReadPointer(0, bufferToFill.startSample),
                (size_t)bufferToFill.numSamples * sizeof(float)
            );
        }
    }
}

void MainComponent::releaseResources()
{
    // This will be called when the audio device stops, or when it is being
    // restarted due to a setting change.

    // For more details, see the help for AudioProcessor::releaseResources()
    buttonSynths.clear();
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    //// (Our component is opaque, so we must completely fill the background with a solid colour)
    //g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    //// You can add your drawing code here!
}

void MainComponent::resized()
{
    // This is called when the MainContentComponent is resized.
    // If you add any child components, this is where you should
    // update their positions.
    auto x = 20;
    auto top = 20;
    auto bottomMargin = 20;
    auto numButtons = buttonLines.size();
    if (numButtons == 0) return;
    auto availableHeight = getHeight() - top - bottomMargin;
    auto spacing = (numButtons > 1) ? (availableHeight - buttonLines[0]->getHeight()) / (numButtons - 1) : 0;
    for (int i = 0; i < numButtons; ++i)
    {
        auto* button = buttonLines[i];
        int y = (int)(top + i * spacing);
        button->setTopLeftPosition(x, y);
        button->setSize(getWidth() - 2 * x, 20);
        addAndMakeVisible(button);
    }
}

void MainComponent::mouseDown(const juce::MouseEvent& e)
{
    mouseDrag(e);
}

void MainComponent::mouseDrag(const juce::MouseEvent& e)
{
    for (int i = 0; i < buttonLines.size(); ++i)
    {
        auto* line = buttonLines[i];
        if (line->getBounds().contains(e.getPosition()))
        {
            auto pos = (e.position.x - (float)line->getX()) / (float)line->getWidth();
            line->clickButton(pos);
            buttonSynths[i]->clickButton(pos);
        }
    }
}

MainComponent::ButtonParameters::ButtonParameters(int midiNoteIn)
    : midiNote(midiNoteIn),
    frequencyInHz(juce::MidiMessage::getMidiNoteInHertz(midiNoteIn)),
    lengthInPixels((int)(
        760 / (frequencyInHz /
            juce::MidiMessage::getMidiNoteInHertz(42))))
{
}

juce::Array<MainComponent::ButtonParameters>
MainComponent::getDefaultButtonParameters()
{
    std::vector<juce::String> targetNotes = { "D", "F", "G", "A", "C" };
    juce::Array<ButtonParameters> scale;
    for (int midi : MidiNotes::Piano88Keys)
    {
        juce::String fullName = juce::MidiMessage::getMidiNoteName(midi, true, true, true);
        juce::String root = fullName.retainCharacters("ABCDEFG#s");

        // 스케일에 포함되는 음만 선택
        if (std::find(
            targetNotes.begin(),
            targetNotes.end(),
            root) != targetNotes.end())
        {
            scale.add(ButtonParameters(midi));
        }
    }
    return scale;
}

void MainComponent::createButtonComponents()
{
    for (auto sp : getDefaultButtonParameters())
    {
        float hue = (sp.midiNote % 12) / 12.0f;
        auto colour = juce::Colour::fromHSV(hue, 0.8f, 0.9f, 1.0f);
        buttonLines.add(new ButtonComponent(sp.lengthInPixels, colour));
    }
}

void MainComponent::generateButtonSynths(double sampleRate)
{
    buttonSynths.clear();

    for (auto bp : getDefaultButtonParameters())
        buttonSynths.add(
            new ButtonSynthesizer(
                sampleRate, bp.frequencyInHz));
}