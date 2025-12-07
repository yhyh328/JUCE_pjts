#pragma once
#include <vector>
#include <JuceHeader.h>

// µû·Î ¼±¾ðÇÑ Å¬·¡½º ½ÃÀÛ

//==============================================================================
// Karplus?Strong ¹°¸® ¸ðµ¨·Î ÁÙ ¼Ò¸®¸¦ ³»´Â ÇÕ¼º±â
class StringSynthesiser
{
public:
    StringSynthesiser(double sampleRate, double frequencyInHz);
    void stringPlucked(float pluckPosition);
    void generateAndAddData(float* outBuffer, int numSamples);
private:
	void prepareSynthesiserState(double sampleRate, double frequencyInHz);
    void exciteInternalBuffer();
    const float decay = 0.998f;
    float amplitude = 0.0f;
    juce::Atomic<int> doPluckForNextBuffer;
    std::vector<float> excitationSample, delayLine;
    size_t pos = 0;
};

//==============================================================================
// È­¸é¿¡ ÁÙÀÇ Áøµ¿À» ±×·ÁÁÖ´Â ÄÄÆ÷³ÍÆ®
class StringComponent : public juce::Component, private juce::Timer
{
public:
    StringComponent(int lengthInPixels, juce::Colour stringColour);
    void stringPlucked(float pos);
    void paint(juce::Graphics& g) override;
private:
    juce::Path generateStringPath() const;
    void timerCallback() override;

    int length;
    juce::Colour colour;

    int height = 20;
    float amplitude = 0.0f;
    const float maxAmplitude = 12.0f;
    float phase = 0.0f;
};


// µû·Î ¼±¾ðÇÑ Å¬·¡½º ³¡


//==============================================================================
/*
    This component lives inside our window, and this is where you should put all
    your controls and content.
*/
//==============================================================================
// ¸ÞÀÎ ¿Àµð¿À/GUI ÄÄÆ÷³ÍÆ®
class MainComponent  : public juce::AudioAppComponent
{
public:
    
    struct StringParameters
    {
        int midiNote;
        double frequencyInHz;
        int lengthInPixels;

        StringParameters(int midiNoteIn); // ← 선언만
    };

    //==============================================================================
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
	// µû·Î ¼±¾ðÇÑ ¸â¹ö º¯¼öµé
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

    static juce::Array<StringParameters> getDefaultStringParameters();
    void createStringComponents();
    void generateStringSynths(double sampleRate);

    juce::OwnedArray<StringComponent> stringLines;
    juce::OwnedArray<StringSynthesiser> stringSynths;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
