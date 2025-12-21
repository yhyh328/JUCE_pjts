#pragma once

#include <vector>
#include <JuceHeader.h>

class ButtonSynthesizer
{
public:
    ButtonSynthesizer(double sampleRate, double frequencyInHz);
    void clickButton(float clickPosition);
	void generateAndAddData(float* outBuffer, int numSamples);
private:
	void prepareSynthesizerState(double sampleRate, double frequencyInHz);
    void exciteInternalBuffer();
    const float decay = 0.998f;
    float amplitude = 0.0f;
    juce::Atomic<int> doClickForNextBuffer;
    std::vector<float> excitationSample, delayLine;
    size_t pos = 0;
};

class ButtonComponent : public juce::Component, private juce::Timer
{
public:
    ButtonComponent(int lengthInPixels, juce::Colour buttonColour);
	void clickButton(float pos);
	void paint(juce::Graphics& g) override;
private:
    juce::Path generateButtonPath() const;
	void timerCallback() override;
    int length;
    juce::Colour colour;
    int height = 20;
    float amplitude = 0.0f;
    const float maxAmplitude = 12.0f;
};

class MainComponent : public juce::AudioAppComponent
{
public:
    //==============================================================================
    struct ButtonParameters 
    {
        int midiNote;
        double frequencyInHz;
        int lengthInPixels;
        ButtonParameters(int midiNoteIn);
    };
    MainComponent();
    ~MainComponent() override;

    //==============================================================================
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    //==============================================================================
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    //==============================================================================
    // Your private member variables go here...
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    static juce::Array<ButtonParameters> getDefaultButtonParameters();
    void createButtonComponents();
    void generateButtonSynths(double sampleRate);
    juce::OwnedArray<ButtonComponent> buttonLines;
    juce::OwnedArray<ButtonSynthesizer> buttonSynths;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
