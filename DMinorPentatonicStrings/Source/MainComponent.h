#pragma once
#include <vector>
#include <JuceHeader.h>

// 따로 선언한 클래스 시작

//==============================================================================
// Karplus–Strong(딜레이 라인 기반 물리모델링)으로 줄(플럭) 소리를 내는 합성기
//==============================================================================
// StringSynthesiser
//==============================================================================
// 이 클래스는 "줄을 튕긴(pluck) 소리"를 딜레이 라인 + 피드백으로 모델링한다.
//
// 핵심 아이디어(Karplus–Strong):
//   - delayLine(링버퍼)에 '한 주기(≈ sampleRate/frequency) 길이'의 샘플을 저장해 순환시킨다.
//   - 매 샘플마다 인접 샘플 평균(간단한 저역통과 필터)을 적용하면 고주파가 더 빨리 감쇠하며
//     실제 줄처럼 점점 부드러워지는 소리가 난다.
//   - decay(감쇠 계수)로 에너지 손실을 모델링한다.
//
// 호출 스레드 관점(중요):
//   - stringPlucked(): 보통 GUI/MIDI 등 "이벤트" 스레드에서 호출(비실시간)
//   - generateAndAddData(): 오디오 콜백 스레드에서 매우 자주 호출(실시간)
//   => 이벤트 스레드에서는 "플럭 요청"만 Atomic 플래그로 전달하고,
//      실제 버퍼 주입(excite)은 오디오 스레드의 안전한 지점(버퍼 시작)에서 수행한다.
//
// Atomic::compareAndSetBool(valueToSet, valueToCompare):
//   - 현재 값이 valueToCompare 이면 valueToSet 으로 바꾸고 true 반환
//   - 아니면 바꾸지 않고 false 반환
//

class StringSynthesiser
{
    // DSP를 조작하는 손잡이(API)
public:
    // 생성자: 샘플레이트와 기본 진동수(Hz)로 내부 버퍼(딜레이 라인) 준비
    StringSynthesiser(double sampleRate, double frequencyInHz);
    // 플럭 입력(0.0 ~ 1.0)
    // - 여기서는 pluckPosition을 이용해 '플럭 강도(amplitude)'만 정하는 간단 모델(세기만 반영)
    void stringPlucked(float pluckPosition);
    // 오디오 버퍼에 numSamples 만큼 샘플을 생성해서 더함(add)
    void generateAndAddData(float* outBuffer, int numSamples);
    // DSP (Digital Signal Processing) 내부 상태
private:
    // frequency에 맞는 딜레이 라인 길이 계산 및 excitation 생성
    void prepareSynthesiserState(double sampleRate, double frequencyInHz);
    // delayLine을 excitationSample로 채워서 "플럭"을 시작
    void exciteInternalBuffer();
    // 감쇠 계수(1에 가까울수록 길게 울림)
    const float decay = 0.998f;
    // 플럭 강도(위치에 따라 sin(pi*x)로 계산)
    float amplitude = 0.0f;
    // 플럭 요청 플래그 (0 = 요청 없음, 1 = 다음 버퍼 시작에서 플럭 수행)
    // - GUI/MIDI 스레드에서 요청을 걸고, 오디오 스레드에서 소비(1->0)한다.
    juce::Atomic<int> doPluckForNextBuffer;
    // excitationSample: 플럭 순간의 초기 자극(노이즈/사인 등)
    // delayLine: 한 주기 길이(≈ sampleRate/frequency)의 순환 버퍼(피드백 딜레이 라인)
    std::vector<float> excitationSample, delayLine;
    // 줄의 길이를 저장한 순환 버퍼(delayLine)에서 현재 샘플을 읽고 다음 샘플을 쓸 위치
    size_t pos = 0;
};

//==============================================================================
// 화면에 줄의 진동을 그려주는 컴포넌트
//==============================================================================
// StringComponent
//==============================================================================
//
// 화면에 "줄이 진동하는 것처럼 보이는" 시각적 표현을 담당하는 컴포넌트.
//
// 주의(중요):
//   - 이 클래스는 오디오 DSP와는 완전히 분리된 "시각화 전용" 컴포넌트이다.
//   - 내부의 amplitude / phase 값은 실제 소리(StringSynthesiser의 delayLine 등)
//     과는 직접적인 연관이 없으며, 단지 시각적으로 그럴듯한 애니메이션을
//     만들기 위한 값들이다.
//
// 동작 개요:
//   - Timer를 사용해 초당 60회 화면을 갱신한다.
//   - 중앙을 기준으로 sin(phase)에 따라 위아래로 휘어진 곡선을 그려
//     "줄의 진동"을 단순화해 표현한다.
//

class StringComponent : public juce::Component, private juce::Timer
{
public:
    // lengthInPixels : 화면 상에서 줄의 길이(픽셀 단위)
    // stringColour   : 줄을 그릴 색상
    StringComponent(int lengthInPixels, juce::Colour stringColour);

    // 화면 상에서 줄을 "튕겼을 때" 호출
    // pos (0.0 ~ 1.0):
    //   - 클릭 위치에 따라 시각적 진폭을 결정
    //   - 0.0 / 1.0 (양 끝)에서는 진폭이 0
    //   - 0.5 (가운데)에서 최대
    void stringPlucked(float pos);

    // JUCE Component: 화면에 줄을 그림
    void paint(juce::Graphics& g) override;

private:
    // 현재 phase / amplitude 상태를 바탕으로
    // 줄 모양(곡선 Path)을 생성
    juce::Path generateStringPath() const;

    // Timer 콜백 (약 60Hz):
    //   - 시각적 진폭 감쇠
    //   - 위상(phase) 진행
    //   - repaint() 호출로 화면 갱신
    void timerCallback() override;

    // ====== 시각화 상태 변수들 ======

    // 줄의 길이(픽셀 단위)
    int length;

    // 줄 색상
    juce::Colour colour;

    // 컴포넌트 높이(픽셀)
    int height = 20;

    // 현재 시각적 진폭(픽셀 단위)
    float amplitude = 0.0f;

    // 시각적 진폭의 최대값(클리핑 방지 및 연출용)
    const float maxAmplitude = 12.0f;

    // 시각적 진동 위상 (0 ~ 2π 반복)
    float phase = 0.0f;
};


// 따로 선언한 클래스 끝


//==============================================================================
/*
    This component lives inside our window, and this is where you should put all
    your controls and content.
*/
//==============================================================================
// MainComponent
//
// 이 클래스는 애플리케이션의 "중심"이 되는 컴포넌트로,
//  - 오디오 입출력(AudioAppComponent)
//  - GUI 렌더링
//  - 마우스 입력
//  - StringSynthesiser(DSP)와 StringComponent(시각화)를
//    함께 관리한다.
//
// 역할 분리 개념:
//  - StringSynthesiser : 소리를 만드는 DSP (오디오 스레드)
//  - StringComponent   : 줄의 진동을 그리는 GUI (메시지/타이머 스레드)
//  - MainComponent     : 둘을 연결하고 전체 흐름을 제어
//
class MainComponent : public juce::AudioAppComponent
{
public:

    //--------------------------------------------------------------------------
    // StringParameters
    //
    // 하나의 "줄(String)"에 대한 정적 설정값 묶음
    //  - midiNote        : 음 높이(MIDI 노트 번호)
    //  - frequencyInHz   : 실제 재생에 사용할 주파수
    //  - lengthInPixels  : 화면에 그릴 줄의 길이
    //
    // 이 구조체는:
    //  - DSP(StringSynthesiser) 생성 시
    //  - GUI(StringComponent) 생성 시
    // 공통 파라미터로 사용된다.
    //
    struct StringParameters
    {
        int midiNote;           // MIDI 노트 번호 (예: 62 = D4)
        double frequencyInHz;   // 해당 노트의 주파수 (Hz)
        int lengthInPixels;     // 화면 상에서 줄의 길이

        // midiNote만 주어지면,
        // 내부에서 frequency / length를 계산해 채운다.
        StringParameters(int midiNoteIn);
    };

    //==============================================================================
    // 생성자 / 소멸자
    //
    // - 생성자: 오디오 장치 초기화, 줄 데이터 구조 준비
    // - 소멸자: JUCE 리소스 정리
    //
    MainComponent();
    ~MainComponent() override;

    //==============================================================================
    // AudioAppComponent 인터페이스
    //
    // 이 세 함수는 "오디오 스레드"에서 호출된다.
    // 절대 느린 작업(할당, 로그, GUI 접근 등)을 하면 안 된다.
    //

    // 오디오 장치가 시작될 때 한 번 호출
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;

    // 오디오 콜백:
    // 매 블록마다 호출되며 실제 소리를 생성
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

    // 오디오 장치가 멈출 때 호출
    void releaseResources() override;

    //==============================================================================
    // GUI 관련 콜백
    //

    // MainComponent 자체 배경 등을 그릴 때 호출
    void paint(juce::Graphics& g) override;

    // 컴포넌트 크기 변경 시,
    // 자식 컴포넌트(StringComponent) 배치를 조정
    void resized() override;

private:
    //==============================================================================
    // 마우스 입력 처리
    //
    // - mouseDown : 줄을 처음 클릭했을 때
    // - mouseDrag : 줄을 드래그하면서 플럭 위치 변경
    //
    // 이 입력은:
    //  - 시각화(StringComponent)
    //  - 소리(StringSynthesiser)
    // 양쪽에 동시에 전달된다.
    //
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

    //==============================================================================
    // 내부 헬퍼 함수들
    //

    // 기본 줄 세트(D minor pentatonic 등)를 생성
    // 각 줄의 MIDI 노트, 길이 등을 미리 정의
    static juce::Array<StringParameters> getDefaultStringParameters();

    // StringParameters를 기반으로
    // 화면에 StringComponent들을 생성하고 배치
    void createStringComponents();

    // StringParameters를 기반으로
    // 각 줄에 대응하는 StringSynthesiser(DSP)를 생성
    void generateStringSynths(double sampleRate);

    //==============================================================================
    // 멤버 변수
    //

    // 화면에 보이는 줄들 (GUI)
    juce::OwnedArray<StringComponent> stringLines;

    // 각 줄에 대응하는 DSP 신스
    juce::OwnedArray<StringSynthesiser> stringSynths;

    // 복사/대입 방지 + 메모리 누수 검사(JUCE 매크로)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};