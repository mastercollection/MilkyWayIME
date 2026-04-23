#pragma once

namespace milkyway::engine::key {

enum class BaseLayoutKey {
  kUnknown,
  kA,
  kB,
  kC,
  kD,
  kE,
  kF,
  kG,
  kH,
  kI,
  kJ,
  kK,
  kL,
  kM,
  kN,
  kO,
  kP,
  kQ,
  kR,
  kS,
  kT,
  kU,
  kV,
  kW,
  kX,
  kY,
  kZ,
  kDigit0,
  kDigit1,
  kDigit2,
  kDigit3,
  kDigit4,
  kDigit5,
  kDigit6,
  kDigit7,
  kDigit8,
  kDigit9,
  kSpace,
  kTab,
  kReturn,
  kBackspace,
  kOem1,
  kOem2,
  kOem3,
  kOem4,
  kOem5,
  kOem6,
  kOem7,
};

constexpr bool IsAlphabeticBaseLayoutKey(BaseLayoutKey key) {
  return key >= BaseLayoutKey::kA && key <= BaseLayoutKey::kZ;
}

constexpr char BaseLayoutKeyToAsciiLetter(BaseLayoutKey key, bool shift) {
  if (!IsAlphabeticBaseLayoutKey(key)) {
    return 0;
  }

  return static_cast<char>((shift ? 'A' : 'a') +
                           (static_cast<int>(key) -
                            static_cast<int>(BaseLayoutKey::kA)));
}

constexpr const char* BaseLayoutKeyName(BaseLayoutKey key) {
  switch (key) {
    case BaseLayoutKey::kUnknown:
      return "Unknown";
    case BaseLayoutKey::kA:
      return "A";
    case BaseLayoutKey::kB:
      return "B";
    case BaseLayoutKey::kC:
      return "C";
    case BaseLayoutKey::kD:
      return "D";
    case BaseLayoutKey::kE:
      return "E";
    case BaseLayoutKey::kF:
      return "F";
    case BaseLayoutKey::kG:
      return "G";
    case BaseLayoutKey::kH:
      return "H";
    case BaseLayoutKey::kI:
      return "I";
    case BaseLayoutKey::kJ:
      return "J";
    case BaseLayoutKey::kK:
      return "K";
    case BaseLayoutKey::kL:
      return "L";
    case BaseLayoutKey::kM:
      return "M";
    case BaseLayoutKey::kN:
      return "N";
    case BaseLayoutKey::kO:
      return "O";
    case BaseLayoutKey::kP:
      return "P";
    case BaseLayoutKey::kQ:
      return "Q";
    case BaseLayoutKey::kR:
      return "R";
    case BaseLayoutKey::kS:
      return "S";
    case BaseLayoutKey::kT:
      return "T";
    case BaseLayoutKey::kU:
      return "U";
    case BaseLayoutKey::kV:
      return "V";
    case BaseLayoutKey::kW:
      return "W";
    case BaseLayoutKey::kX:
      return "X";
    case BaseLayoutKey::kY:
      return "Y";
    case BaseLayoutKey::kZ:
      return "Z";
    case BaseLayoutKey::kDigit0:
      return "Digit0";
    case BaseLayoutKey::kDigit1:
      return "Digit1";
    case BaseLayoutKey::kDigit2:
      return "Digit2";
    case BaseLayoutKey::kDigit3:
      return "Digit3";
    case BaseLayoutKey::kDigit4:
      return "Digit4";
    case BaseLayoutKey::kDigit5:
      return "Digit5";
    case BaseLayoutKey::kDigit6:
      return "Digit6";
    case BaseLayoutKey::kDigit7:
      return "Digit7";
    case BaseLayoutKey::kDigit8:
      return "Digit8";
    case BaseLayoutKey::kDigit9:
      return "Digit9";
    case BaseLayoutKey::kSpace:
      return "Space";
    case BaseLayoutKey::kTab:
      return "Tab";
    case BaseLayoutKey::kReturn:
      return "Return";
    case BaseLayoutKey::kBackspace:
      return "Backspace";
    case BaseLayoutKey::kOem1:
      return "Oem1";
    case BaseLayoutKey::kOem2:
      return "Oem2";
    case BaseLayoutKey::kOem3:
      return "Oem3";
    case BaseLayoutKey::kOem4:
      return "Oem4";
    case BaseLayoutKey::kOem5:
      return "Oem5";
    case BaseLayoutKey::kOem6:
      return "Oem6";
    case BaseLayoutKey::kOem7:
      return "Oem7";
  }

  return "Unknown";
}

}  // namespace milkyway::engine::key
