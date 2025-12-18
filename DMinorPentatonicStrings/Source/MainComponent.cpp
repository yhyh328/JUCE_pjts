#include "MainComponent.h"
#include "Piano88Keys.h"
#include <cmath>
#include <algorithm>
#include <cstring>

//==============================================================================
// StringSynthesiser 구현 (Karplus–Strong)
//==============================================================================

// 생성자: 샘플레이트/주파수에 맞게 내부 링버퍼(delayLine) 및 excitation 준비
StringSynthesiser::StringSynthesiser(double sampleRate, double frequencyInHz)
{
    // 0 = 플럭 요청 없음, 1 = 다음 오디오 버퍼 시작에서 플럭 수행
    doPluckForNextBuffer.set(0);
    prepareSynthesiserState(sampleRate, frequencyInHz);
}

// 플럭 입력(0.0 ~ 1.0)
// - 여기서는 pluckPosition을 이용해 "플럭 강도(amplitude)"만 정하는 간단 모델(세기만 반영)
// - 실제 줄은 플럭 위치에 따라 배음 분포(음색)도 바뀌지만, 그 모델링은 아직 하지 않음.
void StringSynthesiser::stringPlucked(float pluckPosition)
{
    jassert(pluckPosition >= 0.0f && pluckPosition <= 1.0f);

    // 0 -> 1로 바꿀 수 있으면(=현재 요청 없음이면) 플럭 요청을 건다.
    // 이미 1이면(요청이 이미 걸려 있으면) 이번 이벤트는 무시한다.
    if (doPluckForNextBuffer.compareAndSetBool(1, 0))
    {
        // sin(pi*x): x=0,1에서 0 / x=0.5에서 최대
        // => 가운데를 뜯을수록 더 큰 진폭(세기)
        amplitude = std::sin(juce::MathConstants<float>::pi * pluckPosition);
    }
}

// 오디오 버퍼에 numSamples 만큼 샘플을 생성해서 더함(add)
// - outBuffer[i]에 "+=" 하는 이유: 여러 소스(여러 줄/여러 보이스)를 믹스 가능하게 하기 위함.
// - 이 함수는 오디오 콜백에서 호출된다고 가정: 동적 할당/파일IO/로그/락 등 금지.
void StringSynthesiser::generateAndAddData(float* outBuffer, int numSamples)
{
    // 1 -> 0으로 바꿀 수 있으면(=플럭 요청이 있으면) 지금 버퍼 시작에서 한 번만 excite 수행
    if (doPluckForNextBuffer.compareAndSetBool(0, 1))
        exciteInternalBuffer();

    if (delayLine.empty())
        return;

    // Karplus–Strong 루프(샘플 단위 업데이트)
    for (int i = 0; i < numSamples; ++i)
    {
        const auto nextPos = (pos + 1) % delayLine.size();

        // 인접 샘플 평균(간단 저역통과) + 감쇠
        delayLine[nextPos] = decay * 0.5f * (delayLine[nextPos] + delayLine[pos]);

        // 현재 샘플 출력
        outBuffer[i] += delayLine[pos];

        // 시간 진행(커서 이동)
        pos = nextPos;
    }
}

// frequency에 맞는 딜레이 라인 길이 계산 및 excitation 생성
void StringSynthesiser::prepareSynthesiserState(double sampleRate, double frequencyInHz)
{
    // 한 주기 샘플 수(정수 반올림). 간단하지만 튜닝 오차가 생길 수 있음.
    // 더 정확한 피치가 필요하면 fractional delay/all-pass 등을 고려.
    const auto delayLineLength = (size_t)std::max(2.0, std::round(sampleRate / frequencyInHz));

    // 최소 길이 보장: 너무 짧은 delayLine은 KS가 부자연스럽고 쉽게 깨질 수 있다.
    // (기존처럼 >50 같은 큰 임계값을 걸면 특정 고음이 아예 막힐 수 있음)
    jassert(delayLineLength >= 2);

    delayLine.assign(delayLineLength, 0.0f);
    excitationSample.resize(delayLineLength);

    // excitationSample:
    // 플럭 순간에 주입되는 초기 에너지(초기 변위/속도)를 단순화해 표현한 것.
    // 랜덤 노이즈는 넓은 대역 에너지를 주입하여 풍부한 배음을 만들기 쉽다.
    std::generate(excitationSample.begin(),
        excitationSample.end(),
        [] { return (juce::Random::getSystemRandom().nextFloat() * 2.0f) - 1.0f; });

    // ---- 옵션: 사인파 excitation으로 바꾸고 싶다면 아래 블록을 사용 ----
     //float phase = 0.0f;
     //float phaseDelta = juce::MathConstants<float>::twoPi / (float) excitationSample.size();
     //std::generate(excitationSample.begin(), excitationSample.end(),
     //              [&]() {
     //                  float s = std::sin(phase);
     //                  phase += phaseDelta;
     //                  return s;
     //              });
}

// delayLine을 excitationSample로 채워서 "플럭"을 시작
void StringSynthesiser::exciteInternalBuffer()
{
    jassert(delayLine.size() == excitationSample.size());

    // 플럭 시작 시 커서를 리셋하면 시작 위상이 일관되어 재현성이 좋아지고,
    // 불필요한 클릭/위상 느낌을 줄일 수 있다.
    pos = 0;

    // amplitude(플럭 강도)로 스케일해서 초기 에너지를 delayLine에 주입
    std::transform(excitationSample.begin(),
        excitationSample.end(),
        delayLine.begin(),
        [this](float sample) { return amplitude * sample; });
}

//==============================================================================
// StringComponent 구현 (화면에 줄의 진동을 그려주는 부분)
//==============================================================================
//
// 이 컴포넌트는 실제 오디오 DSP와 분리된 "시각화 전용" 클래스이다.
// 내부의 phase / amplitude는 소리의 실제 파형이나 delayLine 상태를
// 직접 표현하지 않으며, 사용자가 줄을 튕겼을 때의 느낌을
// 시각적으로 직관화하기 위한 애니메이션 상태값이다.
//
// 설계 의도:
//   - 실제 줄의 모든 진동 모드를 정확히 그리기보다는,
//     "중앙이 가장 크게 흔들리는 단일 모드"를 단순화해 표현한다.
//   - 중앙 제어점(quadratic Bezier curve)을 sin(phase)로 위아래 이동시켜
//     줄이 진동하는 듯한 착시 효과를 만든다.
//
// 타이밍:
//   - juce::Timer를 사용해 약 60Hz로 화면을 갱신한다.
//   - 이는 화면 리프레시(시각적 부드러움)를 위한 것이며,
//     오디오 샘플레이트와는 전혀 관계가 없다.
//

StringComponent::StringComponent(int lengthInPixels, juce::Colour stringColour)
    : length(lengthInPixels), colour(stringColour)
{
    // 이 컴포넌트는 입력을 받지 않는 순수 시각화 요소이므로
    // 마우스 클릭을 가로채지 않게 설정
    setInterceptsMouseClicks(false, false);

    // 컴포넌트 크기 설정:
    //   - width  : 줄의 길이(픽셀)
    //   - height : 줄의 최대 변위 범위를 고려한 고정 높이
    setSize(length, height);

    // 초당 60회 타이머 콜백
    // (GUI 애니메이션용으로 충분히 부드러운 값)
    startTimerHz(60);
}

void StringComponent::paint(juce::Graphics& g)
{
    // colour:
    // 이 컴포넌트 자체에서 결정되지 않으며,
    // 상위 로직에서 음의 pitch class(계이름)에 따라 미리 정해져 전달된다.
    g.setColour(colour);

    // 현재 phase / amplitude 상태를 기반으로 생성한 Path를 그린다.
    // PathStrokeType(2.0f)는 줄의 두께를 의미.
    g.strokePath(generateStringPath(), juce::PathStrokeType(2.0f));
}

juce::Path StringComponent::generateStringPath() const
{
    juce::Path p;

    // 컴포넌트 중앙을 기준선(y)으로 사용
    const float y = (float)height / 2.0f;

    // 시작점(왼쪽 끝)
    p.startNewSubPath(0.0f, y);

    // quadratic Bezier curve:
    //   - 시작점과 끝점은 고정
    //   - 제어점(control point)을 위아래로 움직여
    //     줄이 휘어지는 것처럼 보이게 한다.
    //
    // sin(phase) * amplitude:
    //   - phase에 따라 주기적으로 위아래 진동
    //   - amplitude는 시각적 진폭(픽셀 단위)
    p.quadraticTo((float)length / 2.0f,
        y + std::sin(phase) * amplitude,
        (float)length,
        y);

    return p;
}

void StringComponent::timerCallback()
{
    // 시각적 진폭 감쇠:
    // 실제 줄이 에너지를 잃는 것처럼 보이게 하기 위한 연출
    amplitude *= 0.99f;

    // 시각적 진동 위상 진행
    // length에 반비례시켜, 짧은 줄일수록 빠르게 흔들리는 것처럼 보이게 함
    phase += 400.0f / (float)length;

    // 위상 래핑 (0 ~ 2π)
    if (phase >= juce::MathConstants<float>::twoPi)
        phase -= juce::MathConstants<float>::twoPi;

    // 상태가 바뀌었으므로 다시 그리기 요청
    repaint();
}

void StringComponent::stringPlucked(float pos)
{
    // pos: 0.0 ~ 1.0 (왼쪽 끝 ~ 오른쪽 끝)
    //
    // sin(pi * pos):
    //   - 양 끝에서는 0
    //   - 중앙(0.5)에서 최대
    // => 중앙을 튕겼을 때 가장 크게 흔들리는 시각 효과
    amplitude = maxAmplitude * std::sin(pos * juce::MathConstants<float>::pi);

    // 위상을 π로 설정해
    // "튀어 오르는 순간"처럼 보이게 연출
    phase = juce::MathConstants<float>::pi;
}

//==============================================================================
// MainComponent 구현
//==============================================================================
//
// MainComponent는 이 애플리케이션의 컨트롤 타워 역할을 한다.
//  - 오디오 디바이스 초기화
//  - StringSynthesiser(DSP) 관리
//  - StringComponent(GUI) 배치 및 입력 연결
//  - 마우스 입력 → 소리 + 시각화로 전달
//

MainComponent::MainComponent()
{
    // 줄(String)에 대한 GUI 컴포넌트 생성
    // (아직 오디오 디바이스는 시작되지 않은 상태)
    createStringComponents();

    // 자식 컴포넌트를 추가한 뒤에 전체 컴포넌트 크기 설정
    setSize(800, 560);

    // 현재 사용 중인 오디오 디바이스 정보 가져오기
    auto* audioDevice = deviceManager.getCurrentAudioDevice();

    // 입력 채널 수 (없으면 0)
    auto numInputs = audioDevice
        ? audioDevice->getActiveInputChannels().countNumberOfSetBits()
        : 0;

    // 출력 채널 수 (없으면 기본 2)
    auto numOutputs = audioDevice
        ? audioDevice->getActiveOutputChannels().countNumberOfSetBits()
        : 2;

    // 오디오 입출력 채널 설정
    // 출력은 최소 2채널(스테레오) 보장
    setAudioChannels(numInputs, std::max(2, numOutputs));

    // (모바일 플랫폼용 마이크 권한 요청 코드 – 현재는 사용 안 함)
}

MainComponent::~MainComponent()
{
    // 오디오 디바이스를 안전하게 종료
    shutdownAudio();
}

//==============================================================================
// 오디오 라이프사이클
//==============================================================================

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    // 오디오 디바이스가 시작되거나,
    // 샘플레이트/버퍼 크기가 바뀔 때 호출됨
    //
    // ⚠️ 오디오 스레드에서 호출되므로:
    //  - 메모리 할당
    //  - GUI 접근
    //  - 로그 출력
    // 같은 작업은 피해야 함

    // 샘플레이트에 맞춰 각 줄의 DSP 신스 생성
    generateStringSynths(sampleRate);
}

void MainComponent::getNextAudioBlock(
    const juce::AudioSourceChannelInfo& bufferToFill)
{
    // 매 오디오 블록마다 호출되는 핵심 DSP 함수
    // 반드시 "시간 엄수"가 필요함

    // 우선 버퍼를 0으로 클리어
    bufferToFill.clearActiveBufferRegion();

    for (int ch = 0; ch < bufferToFill.buffer->getNumChannels(); ++ch)
    {
        // 현재 채널의 쓰기 포인터
        float* channelData =
            bufferToFill.buffer->getWritePointer(
                ch, bufferToFill.startSample);

        if (ch == 0)
        {
            // 0번 채널: 실제 소리 합성
            // 모든 줄(StringSynthesiser)의 출력을 합산
            for (auto* syn : stringSynths)
            {
                syn->generateAndAddData(
                    channelData,
                    bufferToFill.numSamples);
            }
        }
        else
        {
            // 나머지 채널:
            // 0번 채널을 그대로 복사 (모노 → 스테레오)
            std::memcpy(
                channelData,
                bufferToFill.buffer->getReadPointer(
                    0, bufferToFill.startSample),
                (size_t)bufferToFill.numSamples * sizeof(float));
        }
    }
}

void MainComponent::releaseResources()
{
    // 오디오 디바이스가 멈출 때 호출됨
    // DSP 리소스 정리
    stringSynths.clear();
}

//==============================================================================
// GUI
//==============================================================================

void MainComponent::paint(juce::Graphics& g)
{
    // 배경은 LookAndFeel 기본값 사용
    // (필요하면 여기서 fillAll 가능)
}

void MainComponent::resized()
{
    // 창 크기가 변경될 때 호출됨
    // 자식 컴포넌트(StringComponent) 재배치

    auto x = 20;
    auto top = 20;
    auto bottomMargin = 20;

    auto numStrings = stringLines.size();
    if (numStrings == 0)
        return;

    auto availableHeight = getHeight() - top - bottomMargin;

    // 줄 사이 간격 계산
    auto spacing = (numStrings > 1)
        ? (availableHeight - stringLines[0]->getHeight())
        / (numStrings - 1)
        : 0;

    for (int i = 0; i < numStrings; ++i)
    {
        auto* str = stringLines[i];
        int y = (int)(top + i * spacing);

        str->setTopLeftPosition(x, y);
        str->setSize(getWidth() - 2 * x, 20);

        // 화면에 표시
        addAndMakeVisible(str);
    }
}

//==============================================================================
// 마우스 입력 → 소리 + 시각화
//==============================================================================

void MainComponent::mouseDown(const juce::MouseEvent& e)
{
    // 클릭과 드래그를 동일하게 처리
    mouseDrag(e);
}

void MainComponent::mouseDrag(const juce::MouseEvent& e)
{
    for (int i = 0; i < stringLines.size(); ++i)
    {
        auto* line = stringLines[i];

        // 마우스가 해당 줄 위에 있는지 확인
        if (line->getBounds().contains(e.getPosition()))
        {
            // 줄 안에서의 상대 위치 (0.0 ~ 1.0)
            auto pos =
                (e.position.x - (float)line->getX())
                / (float)line->getWidth();

            // 시각적 줄 튕김
            line->stringPlucked(pos);

            // DSP 줄 튕김
            stringSynths[i]->stringPlucked(pos);
        }
    }
}

//==============================================================================
// Tuning System (음 높이 & 화면 길이 결정)
//==============================================================================

MainComponent::StringParameters::StringParameters(int midiNoteIn)
    : midiNote(midiNoteIn),
    // MIDI 노트 → 실제 주파수
    frequencyInHz(
        juce::MidiMessage::getMidiNoteInHertz(midiNoteIn)),
    // 기준 노트(예: MIDI 42)를 기준으로
    // 저음일수록 줄이 길어지게 계산
    lengthInPixels((int)(
        760 / (frequencyInHz /
            juce::MidiMessage::getMidiNoteInHertz(42))))
{
}

//==============================================================================
// 기본 스케일 정의 (D minor pentatonic)
//==============================================================================

juce::Array<MainComponent::StringParameters>
MainComponent::getDefaultStringParameters()
{
    // D minor pentatonic: D F G A C
    std::vector<juce::String> targetNotes =
    { "D", "F", "G", "A", "C" };

    juce::Array<StringParameters> scale;

    for (int midi : MidiNotes::Piano88Keys)
    {
        // MIDI 번호 → "D3", "F#4" 같은 문자열
        juce::String fullName =
            juce::MidiMessage::getMidiNoteName(
                midi, true, true, true);

        // 옥타브 제거 → "D", "F#", ...
        juce::String root =
            fullName.retainCharacters("ABCDEFG#s");

        // 스케일에 포함되는 음만 선택
        if (std::find(
            targetNotes.begin(),
            targetNotes.end(),
            root) != targetNotes.end())
        {
            scale.add(StringParameters(midi));
        }
    }
    return scale;
}

//==============================================================================
// GUI 줄 생성
//==============================================================================

void MainComponent::createStringComponents()
{
    for (auto sp : getDefaultStringParameters())
    {
        // MIDI 노트 % 12 → pitch class
        // 같은 계이름이면 항상 같은 색
        float hue = (sp.midiNote % 12) / 12.0f;

        auto colour = juce::Colour::fromHSV(
            hue, 0.8f, 0.9f, 1.0f);

        stringLines.add(
            new StringComponent(sp.lengthInPixels, colour));
    }
}

//==============================================================================
// DSP 신스 생성
//==============================================================================

void MainComponent::generateStringSynths(double sampleRate)
{
    stringSynths.clear();

    for (auto sp : getDefaultStringParameters())
        stringSynths.add(
            new StringSynthesiser(
                sampleRate, sp.frequencyInHz));
}
