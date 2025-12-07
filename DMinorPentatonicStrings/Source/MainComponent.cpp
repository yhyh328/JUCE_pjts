#include "MainComponent.h"
#include "Piano88Keys.h"
#include <cmath>
#include <algorithm>
#include <cstring>

//==============================================================================
// StringSynthesiser 구현 (Karplus–Strong)
//==============================================================================

StringSynthesiser::StringSynthesiser(double sampleRate, double frequencyInHz)
{
    doPluckForNextBuffer.set(0);
    prepareSynthesiserState(sampleRate, frequencyInHz);
}

void StringSynthesiser::stringPlucked(float pluckPosition)
{
    jassert(pluckPosition >= 0.0f && pluckPosition <= 1.0f);
    // 플럭 요청 플래그가 1일 때만 0으로 바꾸면서 한 번만 처리
    if (doPluckForNextBuffer.compareAndSetBool(1, 0))
        amplitude = std::sin(juce::MathConstants<float>::pi * pluckPosition);
}

void StringSynthesiser::generateAndAddData(float* outBuffer, int numSamples)
{
    // 다음 버퍼에서 플럭해야 하면 내부 버퍼를 excitationSample로 채움 
    if (doPluckForNextBuffer.compareAndSetBool(0, 1))
        exciteInternalBuffer();

    if (delayLine.empty())
        return;

    // Karplus–Strong 루프: 인접 샘플 평균 + 감쇠
    for (int i = 0; i < numSamples; ++i)
    {
        auto nextPos = (pos + 1) % delayLine.size();
        delayLine[nextPos] = decay * 0.5f * (delayLine[nextPos] + delayLine[pos]);
        outBuffer[i] += delayLine[pos];
        pos = nextPos;
    }
}

void StringSynthesiser::prepareSynthesiserState(double sampleRate, double frequencyInHz)
{
    auto delayLineLength = (size_t)std::round(sampleRate / frequencyInHz);
    
    // 너무 짧으면 제대로 된 음이 안 나옴
    jassert(delayLineLength > 50);

    delayLine.resize(delayLineLength);
    std::fill(delayLine.begin(), delayLine.end(), 0.0f);

    excitationSample.resize(delayLineLength);

    // delay 라인의 초기값을 -1 ~ 1 범위의 랜덤 노이즈로 채움
    std::generate(excitationSample.begin(),
                  excitationSample.end(),
                  [] { return (juce::Random::getSystemRandom().nextFloat() * 2.0f) - 1.0f; });

    float phase = 0.0f;
    float phaseDelta = juce::MathConstants<float>::twoPi / excitationSample.size();

    std::generate(excitationSample.begin(),
        excitationSample.end(),
        [&]() {
            float s = std::sin(phase);
            phase += phaseDelta;
            return s;
        });
}

void StringSynthesiser::exciteInternalBuffer()
{
    jassert(delayLine.size() >= excitationSample.size());

    // Karplus–Strong:
    // 노이즈로 채워둔 excitationSample을 amplitude로 스케일해서 delayLine에 복사
    std::transform(excitationSample.begin(),
                   excitationSample.end(),
                   delayLine.begin(),
                   [this](float sample) { return amplitude * sample; });
}

//==============================================================================
// StringComponent 구현 (화면에 줄의 진동을 그려주는 부분)
//==============================================================================

StringComponent::StringComponent(int lengthInPixels, juce::Colour stringColour) :
    length(lengthInPixels), colour(stringColour)
{
    setInterceptsMouseClicks(false, false);
    setSize(length, height);
    startTimerHz(60);  // 초당 60번 리프레시
}

void StringComponent::paint(juce::Graphics& g)
{
    g.setColour(colour);
    g.strokePath(generateStringPath(), juce::PathStrokeType(2.0f));
}

juce::Path StringComponent::generateStringPath() const
{
    juce::Path p;
    float y = (float)height / 2.0f;

    p.startNewSubPath(0.0f, y);
    p.quadraticTo((float)length / 2.0f, 
        y + std::sin(phase) * amplitude, 
        (float)length, 
        y);
    return p;
}

void StringComponent::timerCallback()
{
    // 화면 상에서 보이는 줄의 진폭 감쇠
    amplitude *= 0.99f;

    // 진동 속도 (0.0f ~ 2π 반복)
    phase += 400.0f / (float)length;

    if (phase >= juce::MathConstants<float>::twoPi)
        phase -= juce::MathConstants<float>::twoPi;

    repaint();
}

void StringComponent::stringPlucked(float pos)
{
    // 클릭 위치에 따라 초기 진폭 설정 (양 끝은 0, 가운데가 최대)
    amplitude = maxAmplitude * std::sin(pos * juce::MathConstants<float>::pi);
    phase = juce::MathConstants<float>::pi;
}

//==============================================================================
// MainComponent 구현
//==============================================================================

MainComponent::MainComponent()
{
    createStringComponents();

    // Make sure you set the size of the component after
    // you add any child components.
    setSize (800, 560);

    auto* audioDevice = deviceManager.getCurrentAudioDevice();
    auto numInputs  = audioDevice ? audioDevice->getActiveInputChannels().countNumberOfSetBits() : 0;
    auto numOutputs = audioDevice ? audioDevice->getActiveInputChannels().countNumberOfSetBits() : 2;

    setAudioChannels(numInputs, std::max(2, numOutputs)); // 최소 2채널

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

    generateStringSynths(sampleRate);
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
            // 왼쪽 채널: 실제 합성된 소리
            for (auto* syn : stringSynths)
                syn->generateAndAddData(channelData, 
                                        bufferToFill.numSamples);
        }
        else
        {
            // 나머지 채널은 0번 채널 복사 (모노 -> 스테레오)
            std::memcpy(channelData,
                        bufferToFill.buffer->getReadPointer(0, bufferToFill.startSample),
                        (size_t)bufferToFill.numSamples * sizeof(float));
        }

    }

}

void MainComponent::releaseResources()
{
    // This will be called when the audio device stops, or when it is being
    // restarted due to a setting change.

    // For more details, see the help for AudioProcessor::releaseResources()

    stringSynths.clear();
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    
    // 배경은 따로 안 그림 (LookAndFeel 기본)
    // g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    // You can add your drawing code here!
}

void MainComponent::resized()
{
    // This is called when the MainContentComponent is resized.
    // If you add any child components, this is where you should
    // update their positions.
    auto x = 20;
    auto top = 20;
    auto bottomMargin = 20;

    auto numStrings = stringLines.size();

    if (numStrings == 0)
        return;
    
	auto availableHeight = getHeight() - top - bottomMargin;

	auto spacing = (numStrings > 1) 
        ? (availableHeight - stringLines[0]->getHeight()) / (numStrings - 1) : 
        0;

    for (int i = 0; i < numStrings; ++i)
    {
        auto* str = stringLines[i];
		int y = (int)(top + i * spacing);
        str->setTopLeftPosition(x, y);
		str->setSize(getWidth() - 2 * x, 20); // 가로도 창 크기에 맞게
		addAndMakeVisible(str);
    }

}

void MainComponent::mouseDown(const juce::MouseEvent& e)
{
    mouseDrag(e);
}

void MainComponent::mouseDrag(const juce::MouseEvent& e)
{
    for (int i = 0; i < stringLines.size(); ++i)
    {
        auto* line = stringLines[i];
        if (line->getBounds().contains(e.getPosition()))
        {
            auto pos = (e.position.x - (float)line->getX()) / (float)line->getWidth();
            line->stringPlucked(pos);
            stringSynths[i]->stringPlucked(pos);
        }
    }
}

//==============================================================================
// Tuning System (줄의 음높이, 길이 결정)
//==============================================================================

MainComponent::StringParameters::StringParameters(int midiNoteIn)
    : midiNote(midiNoteIn),
    frequencyInHz(juce::MidiMessage::getMidiNoteInHertz(midiNoteIn)),
    lengthInPixels((int)(760 / (frequencyInHz /
        juce::MidiMessage::getMidiNoteInHertz(42))))
{
}

// ==== ★ 여기서 스케일을 결정한다 ★ ====
juce::Array<MainComponent::StringParameters> MainComponent::getDefaultStringParameters()
{
    // D minor pentatonic: D F G A C
    std::vector<juce::String> targetNotes = { "D", "F", "G", "A", "C" };
    juce::Array<StringParameters> DMinorPentatonicScale;
    for (int midi : MidiNotes::Piano88Keys)
    {   
        // MIDI 번호 → "D2", "F3", "C4" 같은 문자열로
        juce::String fullName = juce::MidiMessage::getMidiNoteName(
            midi,
            true, // use sharp
            true, // include octave number
            true // middle C as C4
        );

        // "D2" → "D", "F3" → "F" 만 남기기
        juce::String root = fullName.retainCharacters("ABCDEFG#s");

        // D, F, G, A, C 중 하나이면 스케일에 포함
        if (std::find(targetNotes.begin(), targetNotes.end(), root) != targetNotes.end())
        {
            DMinorPentatonicScale.add(StringParameters(midi));
        }
    }
    return DMinorPentatonicScale;
}

void MainComponent::createStringComponents()
{
    for (auto sp : getDefaultStringParameters())
        //{
        //    stringLines.add(new StringComponent(sp.lengthInPixels,
        //        juce::Colour::fromHSV(juce::Random().nextFloat(), 0.6f, 0.9f, 1.0f)));
        //}
    {
        float hue = (sp.midiNote % 12) / 12.0f;   // 0.0 ~ 1.0

        auto colour = juce::Colour::fromHSV(
            hue,   // hue: 음 높이에 따른 색상
            0.8f,  // saturation
            0.9f,  // brightness
            1.0f   // alpha
        );
		stringLines.add(new StringComponent(sp.lengthInPixels, colour));
    }
    
}

void MainComponent::generateStringSynths(double sampleRate)
{
    stringSynths.clear();

    for (auto sp : getDefaultStringParameters())
        stringSynths.add(new StringSynthesiser(sampleRate, sp.frequencyInHz));
}