/*
  ==============================================================================

    Piano88Keys.h
    Created: 7 Dec 2025 3:00:17am
    Author:  kimyh

  ==============================================================================
*/

#pragma once

// 88-key piano note names → MIDI 번호 매핑
// 기준: A0 = 21, C4 = 60, A4 = 69, C8 = 108

namespace MidiNotes
{
    enum Notes : int
    {
        A0 = 21, As0 = 22, B0 = 23,

        C1 = 24, Cs1 = 25, D1 = 26, Ds1 = 27, E1 = 28,
        F1 = 29, Fs1 = 30, G1 = 31, Gs1 = 32,
        A1 = 33, As1 = 34, B1 = 35,

        C2 = 36, Cs2 = 37, D2 = 38, Ds2 = 39, E2 = 40,
        F2 = 41, Fs2 = 42, G2 = 43, Gs2 = 44,
        A2 = 45, As2 = 46, B2 = 47,

        C3 = 48, Cs3 = 49, D3 = 50, Ds3 = 51, E3 = 52,
        F3 = 53, Fs3 = 54, G3 = 55, Gs3 = 56,
        A3 = 57, As3 = 58, B3 = 59,

        C4 = 60, Cs4 = 61, D4 = 62, Ds4 = 63, E4 = 64,
        F4 = 65, Fs4 = 66, G4 = 67, Gs4 = 68,
        A4 = 69, As4 = 70, B4 = 71,

        C5 = 72, Cs5 = 73, D5 = 74, Ds5 = 75, E5 = 76,
        F5 = 77, Fs5 = 78, G5 = 79, Gs5 = 80,
        A5 = 81, As5 = 82, B5 = 83,

        C6 = 84, Cs6 = 85, D6 = 86, Ds6 = 87, E6 = 88,
        F6 = 89, Fs6 = 90, G6 = 91, Gs6 = 92,
        A6 = 93, As6 = 94, B6 = 95,

        C7 = 96, Cs7 = 97, D7 = 98, Ds7 = 99, E7 = 100,
        F7 = 101, Fs7 = 102, G7 = 103, Gs7 = 104,
        A7 = 105, As7 = 106, B7 = 107,

        C8 = 108
    };

    // === 88건반 전체 리스트 ===
    // range-based for 문으로 순회 가능
    static constexpr int Piano88Keys[] =
    {
        A0,  As0, B0,
        C1,  Cs1, D1,  Ds1,  E1,  F1,  Fs1,  G1,  Gs1,  A1,  As1, B1,
        C2,  Cs2, D2,  Ds2,  E2,  F2,  Fs2,  G2,  Gs2,  A2,  As2, B2,
        C3,  Cs3, D3,  Ds3,  E3,  F3,  Fs3,  G3,  Gs3,  A3,  As3, B3,
        C4,  Cs4, D4,  Ds4,  E4,  F4,  Fs4,  G4,  Gs4,  A4,  As4, B4,
        C5,  Cs5, D5,  Ds5,  E5,  F5,  Fs5,  G5,  Gs5,  A5,  As5, B5,
        C6,  Cs6, D6,  Ds6,  E6,  F6,  Fs6,  G6,  Gs6,  A6,  As6, B6,
        C7,  Cs7, D7,  Ds7,  E7,  F7,  Fs7,  G7,  Gs7,  A7,  As7, B7,
        C8
    };

    static constexpr int NumKeys = sizeof(Piano88Keys) / sizeof(int);
}