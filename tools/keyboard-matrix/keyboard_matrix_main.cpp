#include <windows.h>

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "adapters/libhangul/hangul_composer.h"
#include "engine/key/layout_key.h"
#include "engine/key/key_analysis.h"
#include "engine/key/physical_key.h"
#include "engine/layout/layout_registry.h"
#include "engine/shortcut/shortcut_resolver.h"
#include "engine/state/modifier_state.h"

namespace {

using milkyway::adapters::libhangul::CreateLibhangulComposer;
using milkyway::engine::key::AnalyzeKeyEvent;
using milkyway::engine::key::KeyAnalysis;
using milkyway::engine::key::KeyCategory;
using milkyway::engine::key::KeyCategoryName;
using milkyway::engine::key::KeyTransition;
using milkyway::engine::key::PhysicalKey;
using milkyway::engine::layout::LayoutRegistry;
using milkyway::engine::shortcut::ShortcutActionDebugName;
using milkyway::engine::state::ModifierState;

struct Options {
  std::string mode;
  std::string physical_layout = "us_qwerty";
  std::string korean_layout = "libhangul:2";
  bool help = false;
};

struct KeySpec {
  std::uint16_t virtual_key = 0;
  ModifierState modifiers;
  bool extended = false;
};

std::string Join(const std::vector<std::string>& values,
                 std::string_view delimiter) {
  std::string output;
  for (const std::string& value : values) {
    if (!output.empty()) {
      output += delimiter;
    }
    output += value;
  }
  return output;
}

std::string Hex(std::uint16_t value) {
  std::ostringstream stream;
  stream << "0x" << std::uppercase << std::hex << std::setw(2)
         << std::setfill('0') << static_cast<unsigned int>(value);
  return stream.str();
}

std::string VirtualKeyName(std::uint16_t virtual_key) {
  if (virtual_key >= 'A' && virtual_key <= 'Z') {
    return std::string(1, static_cast<char>(virtual_key));
  }
  if (virtual_key >= '0' && virtual_key <= '9') {
    return std::string(1, static_cast<char>(virtual_key));
  }

  switch (virtual_key) {
    case VK_BACK:
      return "Backspace";
    case VK_TAB:
      return "Tab";
    case VK_RETURN:
      return "Return";
    case VK_ESCAPE:
      return "Esc";
    case VK_SPACE:
      return "Space";
    case VK_OEM_1:
      return "Oem1";
    case VK_OEM_PLUS:
      return "OemPlus";
    case VK_OEM_COMMA:
      return "OemComma";
    case VK_OEM_MINUS:
      return "OemMinus";
    case VK_OEM_PERIOD:
      return "OemPeriod";
    case VK_OEM_2:
      return "Oem2";
    case VK_OEM_3:
      return "Oem3";
    case VK_OEM_4:
      return "Oem4";
    case VK_OEM_5:
      return "Oem5";
    case VK_OEM_6:
      return "Oem6";
    case VK_OEM_7:
      return "Oem7";
    case VK_SHIFT:
      return "Shift";
    case VK_CONTROL:
      return "Control";
    case VK_MENU:
      return "Alt";
    case VK_LWIN:
      return "LWin";
    case VK_RWIN:
      return "RWin";
    default:
      return Hex(virtual_key);
  }
}

std::string ModifierName(const ModifierState& modifiers) {
  std::vector<std::string> names;
  if (modifiers.ctrl) {
    names.push_back("Ctrl");
  }
  if (modifiers.alt) {
    names.push_back("Alt");
  }
  if (modifiers.shift) {
    names.push_back("Shift");
  }
  if (modifiers.win) {
    names.push_back("Win");
  }
  return names.empty() ? "-" : Join(names, "+");
}

std::string KeyLabel(std::uint16_t virtual_key,
                     const ModifierState& modifiers) {
  std::vector<std::string> names;
  if (modifiers.ctrl) {
    names.push_back("Ctrl");
  }
  if (modifiers.alt) {
    names.push_back("Alt");
  }
  if (modifiers.shift) {
    names.push_back("Shift");
  }
  if (modifiers.win) {
    names.push_back("Win");
  }
  names.push_back(VirtualKeyName(virtual_key));
  return Join(names, "+");
}

std::uint16_t VirtualKeyToScanCode(std::uint16_t virtual_key) {
  return static_cast<std::uint16_t>(
      MapVirtualKeyW(static_cast<UINT>(virtual_key), MAPVK_VK_TO_VSC));
}

PhysicalKey MakePhysicalKey(std::uint16_t virtual_key, bool extended = false) {
  return PhysicalKey{virtual_key, VirtualKeyToScanCode(virtual_key), extended};
}

std::string EffectiveHangulAscii(const KeyAnalysis& analysis) {
  if (analysis.category != KeyCategory::kHangulAscii ||
      !analysis.hangul_input.is_mapped || analysis.hangul_input.ascii == 0) {
    return "-";
  }
  return std::string(1, analysis.hangul_input.ascii);
}

std::unique_ptr<milkyway::adapters::libhangul::HangulComposer> CreateComposer(
    const LayoutRegistry& registry, const Options& options) {
  return CreateLibhangulComposer(
      registry.ResolveLibhangulKeyboardId(options.korean_layout));
}

std::string PreviewSingleHangul(const LayoutRegistry& registry,
                                const Options& options,
                                const KeyAnalysis& analysis) {
  if (analysis.category != KeyCategory::kHangulAscii ||
      !analysis.hangul_input.is_mapped) {
    return "-";
  }

  std::unique_ptr<milkyway::adapters::libhangul::HangulComposer> composer =
      CreateComposer(registry, options);
  if (composer == nullptr) {
    return "<composer-unavailable>";
  }

  const auto result = composer->ProcessAscii(analysis.hangul_input.ascii);
  const std::string preview = result.commit_text + result.preedit_text;
  return preview.empty() ? "-" : preview;
}

KeyAnalysis Analyze(const LayoutRegistry& registry, const Options& options,
                    const KeySpec& key) {
  return AnalyzeKeyEvent(registry, options.physical_layout,
                         options.korean_layout,
                         MakePhysicalKey(key.virtual_key, key.extended),
                         key.modifiers, KeyTransition::kPressed);
}

void PrintRow(const LayoutRegistry& registry, const Options& options,
             const KeySpec& spec) {
  const KeyAnalysis analysis = Analyze(registry, options, spec);
  const PhysicalKey key = analysis.event.key;
  std::cout << KeyLabel(spec.virtual_key, spec.modifiers) << '\t'
            << Hex(key.virtual_key) << '\t' << Hex(key.scan_code) << '\t'
            << (key.extended ? "1" : "0") << '\t'
            << ModifierName(spec.modifiers) << '\t'
            << milkyway::engine::key::LayoutKeyName(
                   analysis.input_label_key)
            << '\t'
            << milkyway::engine::key::LayoutKeyName(
                   analysis.hangul_token_key)
            << '\t' << EffectiveHangulAscii(analysis) << '\t'
            << PreviewSingleHangul(registry, options, analysis) << '\t'
            << KeyCategoryName(analysis.category) << '\t'
            << ShortcutActionDebugName(analysis.shortcut_action) << '\n';
}

std::string PreviewSequence(const LayoutRegistry& registry,
                            const Options& options,
                            const std::vector<KeySpec>& sequence,
                            std::string* ascii_trace,
                            std::string* category_trace) {
  std::unique_ptr<milkyway::adapters::libhangul::HangulComposer> composer =
      CreateComposer(registry, options);
  if (composer == nullptr) {
    return "<composer-unavailable>";
  }

  std::vector<std::string> ascii_values;
  std::vector<std::string> category_values;
  std::string committed;
  for (const KeySpec& spec : sequence) {
    const KeyAnalysis analysis = Analyze(registry, options, spec);
    category_values.push_back(KeyCategoryName(analysis.category));
    if (analysis.category == KeyCategory::kHangulAscii &&
        analysis.hangul_input.is_mapped) {
      ascii_values.push_back(std::string(1, analysis.hangul_input.ascii));
      const auto result = composer->ProcessAscii(analysis.hangul_input.ascii);
      committed += result.commit_text;
    } else {
      ascii_values.push_back("-");
    }
  }

  if (ascii_trace != nullptr) {
    *ascii_trace = Join(ascii_values, " ");
  }
  if (category_trace != nullptr) {
    *category_trace = Join(category_values, " ");
  }

  const std::string preview = committed + composer->GetPreeditText();
  return preview.empty() ? "-" : preview;
}

void PrintSequenceRow(const LayoutRegistry& registry, const Options& options,
                      const std::vector<KeySpec>& sequence) {
  std::vector<std::string> labels;
  for (const KeySpec& spec : sequence) {
    labels.push_back(KeyLabel(spec.virtual_key, spec.modifiers));
  }

  std::string ascii_trace;
  std::string category_trace;
  const std::string preview =
      PreviewSequence(registry, options, sequence, &ascii_trace, &category_trace);
  std::cout << Join(labels, " ") << '\t' << ascii_trace << '\t' << preview
            << '\t' << category_trace << '\n';
}

void PrintUsage() {
  std::cout
      << "Usage:\n"
      << "  mwime_keyboard_matrix.exe matrix [--base ID] [--korean ID]\n"
      << "  mwime_keyboard_matrix.exe watch  [--base ID] [--korean ID]\n";
}

Options ParseArgs(int argc, char** argv) {
  Options options;
  if (argc < 2) {
    options.help = true;
    return options;
  }

  options.mode = argv[1];
  if (options.mode == "-h" || options.mode == "--help") {
    options.help = true;
    return options;
  }

  for (int index = 2; index < argc; ++index) {
    const std::string arg = argv[index];
    if ((arg == "--base" || arg == "--physical") && index + 1 < argc) {
      options.physical_layout = argv[++index];
    } else if (arg == "--korean" && index + 1 < argc) {
      options.korean_layout = argv[++index];
    } else if (arg == "-h" || arg == "--help") {
      options.help = true;
    } else {
      options.help = true;
    }
  }

  return options;
}

bool ValidateLayouts(const LayoutRegistry& registry, const Options& options) {
  if (registry.FindPhysicalLayout(options.physical_layout) == nullptr) {
    std::cerr << "Unknown base layout: " << options.physical_layout << '\n';
    std::cerr << "Available base layouts:";
    for (const auto& layout : registry.physical_layouts()) {
      std::cerr << ' ' << layout.id;
    }
    std::cerr << '\n';
    return false;
  }

  if (registry.FindKoreanLayout(options.korean_layout) == nullptr) {
    std::cerr << "Unknown Korean layout: " << options.korean_layout << '\n';
    std::cerr << "Available Korean layouts:";
    for (const auto& layout : registry.korean_layouts()) {
      std::cerr << ' ' << layout.id;
    }
    std::cerr << '\n';
    return false;
  }

  return true;
}

int RunMatrix(const LayoutRegistry& registry, const Options& options) {
  std::cout << "base_layout=" << options.physical_layout
            << " korean=" << options.korean_layout << '\n';
  std::cout << "key\tvk\tscan\text\tmodifier\tinput_label_key"
               "\thangul_token_key\thangul_ascii"
               "\thangul_preview\tcategory\tshortcut\n";

  for (std::uint16_t key = 'A'; key <= 'Z'; ++key) {
    PrintRow(registry, options, KeySpec{key, {}, false});
    ModifierState shift;
    shift.shift = true;
    PrintRow(registry, options, KeySpec{key, shift, false});
  }

  for (std::uint16_t key = '0'; key <= '9'; ++key) {
    PrintRow(registry, options, KeySpec{key, {}, false});
    ModifierState shift;
    shift.shift = true;
    PrintRow(registry, options, KeySpec{key, shift, false});
  }

  const std::vector<std::uint16_t> oem_keys = {
      VK_OEM_1, VK_OEM_PLUS, VK_OEM_COMMA, VK_OEM_MINUS, VK_OEM_PERIOD,
      VK_OEM_2, VK_OEM_3, VK_OEM_4, VK_OEM_5, VK_OEM_6, VK_OEM_7};
  for (const std::uint16_t key : oem_keys) {
    PrintRow(registry, options, KeySpec{key, {}, false});
    ModifierState shift;
    shift.shift = true;
    PrintRow(registry, options, KeySpec{key, shift, false});
  }

  const std::vector<std::uint16_t> control_keys = {VK_SPACE, VK_RETURN, VK_TAB,
                                                   VK_BACK};
  for (const std::uint16_t key : control_keys) {
    PrintRow(registry, options, KeySpec{key, {}, false});
  }

  std::cout << "\nshortcuts\n";
  ModifierState ctrl_shift;
  ctrl_shift.ctrl = true;
  ctrl_shift.shift = true;
  PrintRow(registry, options, KeySpec{VK_SPACE, ctrl_shift, false});

  ModifierState ctrl_alt;
  ctrl_alt.ctrl = true;
  ctrl_alt.alt = true;
  PrintRow(registry, options, KeySpec{VK_SPACE, ctrl_alt, false});

  std::cout << "\nsequences\n";
  std::cout << "keys\thangul_ascii_trace\thangul_preview\tcategory_trace\n";
  PrintSequenceRow(registry, options, {KeySpec{'G'}, KeySpec{'K'}});
  PrintSequenceRow(registry, options, {KeySpec{'R'}, KeySpec{'K'}});
  ModifierState shift;
  shift.shift = true;
  PrintSequenceRow(registry, options, {KeySpec{'R', shift}, KeySpec{'K'}});
  PrintSequenceRow(registry, options, {KeySpec{'G'}, KeySpec{'K'},
                                       KeySpec{'S'}});

  return 0;
}

ModifierState ModifiersFromConsoleState(DWORD state, WORD virtual_key) {
  ModifierState modifiers;
  modifiers.shift = (state & SHIFT_PRESSED) != 0;
  modifiers.ctrl = (state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
  modifiers.alt = (state & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
  modifiers.win = virtual_key == VK_LWIN || virtual_key == VK_RWIN;
  return modifiers;
}

class ConsoleModeGuard {
 public:
  explicit ConsoleModeGuard(HANDLE input) : input_(input) {
    if (GetConsoleMode(input_, &original_mode_)) {
      valid_ = true;
      DWORD mode = original_mode_;
      mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
      mode |= ENABLE_WINDOW_INPUT;
      SetConsoleMode(input_, mode);
    }
  }

  ~ConsoleModeGuard() {
    if (valid_) {
      SetConsoleMode(input_, original_mode_);
    }
  }

 private:
  HANDLE input_ = INVALID_HANDLE_VALUE;
  DWORD original_mode_ = 0;
  bool valid_ = false;
};

int RunWatch(const LayoutRegistry& registry, const Options& options) {
  HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
  if (input == INVALID_HANDLE_VALUE || input == nullptr) {
    std::cerr << "Console input is unavailable.\n";
    return 1;
  }

  ConsoleModeGuard console_mode(input);
  std::unique_ptr<milkyway::adapters::libhangul::HangulComposer> composer =
      CreateComposer(registry, options);
  if (composer == nullptr) {
    std::cerr << "Failed to create libhangul composer.\n";
    return 1;
  }

  std::cout << "Watching keys. Press Esc to exit.\n";
  std::cout << "base_layout=" << options.physical_layout
            << " korean=" << options.korean_layout << '\n';

  for (;;) {
    INPUT_RECORD record = {};
    DWORD read = 0;
    if (!ReadConsoleInputW(input, &record, 1, &read)) {
      std::cerr << "ReadConsoleInputW failed.\n";
      return 1;
    }

    if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) {
      continue;
    }

    const KEY_EVENT_RECORD& key_event = record.Event.KeyEvent;
    if (key_event.wVirtualKeyCode == VK_ESCAPE) {
      return 0;
    }

    const ModifierState modifiers = ModifiersFromConsoleState(
        key_event.dwControlKeyState, key_event.wVirtualKeyCode);
    const PhysicalKey physical_key{
        static_cast<std::uint16_t>(key_event.wVirtualKeyCode),
        static_cast<std::uint16_t>(key_event.wVirtualScanCode),
        (key_event.dwControlKeyState & ENHANCED_KEY) != 0};

    const KeyAnalysis analysis =
        AnalyzeKeyEvent(registry, options.physical_layout, options.korean_layout,
                        physical_key, modifiers, KeyTransition::kPressed);

    std::string commit;
    std::string preedit = composer->GetPreeditText();
    if (analysis.category == KeyCategory::kHangulAscii &&
        analysis.hangul_input.is_mapped) {
      const auto result = composer->ProcessAscii(analysis.hangul_input.ascii);
      commit = result.commit_text;
      preedit = result.preedit_text;
    } else if (analysis.category == KeyCategory::kBackspace) {
      const auto result = composer->ProcessBackspace();
      commit = result.commit_text;
      preedit = result.preedit_text;
    } else if (!preedit.empty()) {
      commit = composer->Flush();
      composer->Reset();
      preedit.clear();
    }

    std::cout << "key=" << KeyLabel(physical_key.virtual_key, modifiers)
              << " vk=" << Hex(physical_key.virtual_key)
              << " scan=" << Hex(physical_key.scan_code)
              << " ext=" << (physical_key.extended ? "1" : "0")
              << " mod=" << ModifierName(modifiers)
              << " input_label="
              << milkyway::engine::key::LayoutKeyName(
                     analysis.input_label_key)
              << " hangul_token="
              << milkyway::engine::key::LayoutKeyName(
                     analysis.hangul_token_key)
              << " hangul_ascii=" << EffectiveHangulAscii(analysis)
              << " commit=\"" << commit << "\""
              << " preedit=\"" << preedit << "\""
              << " category=" << KeyCategoryName(analysis.category)
              << " shortcut="
              << ShortcutActionDebugName(analysis.shortcut_action)
              << '\n';
  }
}

}  // namespace

int main(int argc, char** argv) {
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);

  const Options options = ParseArgs(argc, argv);
  if (options.help) {
    PrintUsage();
    return options.mode.empty() ? 1 : 0;
  }

  LayoutRegistry registry;
  if (!ValidateLayouts(registry, options)) {
    return 1;
  }

  if (options.mode == "matrix") {
    return RunMatrix(registry, options);
  }
  if (options.mode == "watch") {
    return RunWatch(registry, options);
  }

  std::cerr << "Unknown mode: " << options.mode << '\n';
  PrintUsage();
  return 1;
}
