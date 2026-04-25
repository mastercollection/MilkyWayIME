#pragma once

namespace milkyway::engine::key {

enum class LayoutKey {
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
  kOemPlus,
  kOemComma,
  kOemMinus,
  kOemPeriod,
};

constexpr bool IsAlphabeticLayoutKey(LayoutKey key) {
  return key >= LayoutKey::kA && key <= LayoutKey::kZ;
}

constexpr char LayoutKeyToAsciiLetter(LayoutKey key, bool shift) {
  if (!IsAlphabeticLayoutKey(key)) {
    return 0;
  }

  return static_cast<char>((shift ? 'A' : 'a') +
                           (static_cast<int>(key) -
                            static_cast<int>(LayoutKey::kA)));
}

constexpr const char* LayoutKeyName(LayoutKey key) {
  switch (key) {
    case LayoutKey::kUnknown:
      return "Unknown";
    case LayoutKey::kA:
      return "A";
    case LayoutKey::kB:
      return "B";
    case LayoutKey::kC:
      return "C";
    case LayoutKey::kD:
      return "D";
    case LayoutKey::kE:
      return "E";
    case LayoutKey::kF:
      return "F";
    case LayoutKey::kG:
      return "G";
    case LayoutKey::kH:
      return "H";
    case LayoutKey::kI:
      return "I";
    case LayoutKey::kJ:
      return "J";
    case LayoutKey::kK:
      return "K";
    case LayoutKey::kL:
      return "L";
    case LayoutKey::kM:
      return "M";
    case LayoutKey::kN:
      return "N";
    case LayoutKey::kO:
      return "O";
    case LayoutKey::kP:
      return "P";
    case LayoutKey::kQ:
      return "Q";
    case LayoutKey::kR:
      return "R";
    case LayoutKey::kS:
      return "S";
    case LayoutKey::kT:
      return "T";
    case LayoutKey::kU:
      return "U";
    case LayoutKey::kV:
      return "V";
    case LayoutKey::kW:
      return "W";
    case LayoutKey::kX:
      return "X";
    case LayoutKey::kY:
      return "Y";
    case LayoutKey::kZ:
      return "Z";
    case LayoutKey::kDigit0:
      return "Digit0";
    case LayoutKey::kDigit1:
      return "Digit1";
    case LayoutKey::kDigit2:
      return "Digit2";
    case LayoutKey::kDigit3:
      return "Digit3";
    case LayoutKey::kDigit4:
      return "Digit4";
    case LayoutKey::kDigit5:
      return "Digit5";
    case LayoutKey::kDigit6:
      return "Digit6";
    case LayoutKey::kDigit7:
      return "Digit7";
    case LayoutKey::kDigit8:
      return "Digit8";
    case LayoutKey::kDigit9:
      return "Digit9";
    case LayoutKey::kSpace:
      return "Space";
    case LayoutKey::kTab:
      return "Tab";
    case LayoutKey::kReturn:
      return "Return";
    case LayoutKey::kBackspace:
      return "Backspace";
    case LayoutKey::kOem1:
      return "Oem1";
    case LayoutKey::kOem2:
      return "Oem2";
    case LayoutKey::kOem3:
      return "Oem3";
    case LayoutKey::kOem4:
      return "Oem4";
    case LayoutKey::kOem5:
      return "Oem5";
    case LayoutKey::kOem6:
      return "Oem6";
    case LayoutKey::kOem7:
      return "Oem7";
    case LayoutKey::kOemPlus:
      return "OemPlus";
    case LayoutKey::kOemComma:
      return "OemComma";
    case LayoutKey::kOemMinus:
      return "OemMinus";
    case LayoutKey::kOemPeriod:
      return "OemPeriod";
  }

  return "Unknown";
}

}  // namespace milkyway::engine::key
