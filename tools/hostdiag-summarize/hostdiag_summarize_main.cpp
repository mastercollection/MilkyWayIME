#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Options {
  bool help = false;
  bool self_test = false;
  std::string log_path;
  std::string process_filter;
  std::string since_filter;
  std::string uia_path;
  std::string format = "text";
};

std::string Narrow(const wchar_t* text) {
  if (text == nullptr) {
    return {};
  }
  std::wstring wide(text);
  std::string narrow;
  narrow.reserve(wide.size());
  for (wchar_t ch : wide) {
    narrow.push_back(ch <= 0x7F ? static_cast<char>(ch) : '?');
  }
  return narrow;
}

Options ParseArgs(int argc, wchar_t** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string arg = Narrow(argv[index]);
    if (arg == "--help" || arg == "-h" || arg == "/?") {
      options.help = true;
    } else if (arg == "--self-test") {
      options.self_test = true;
    } else if (arg == "--log" && index + 1 < argc) {
      options.log_path = Narrow(argv[++index]);
    } else if (arg == "--process" && index + 1 < argc) {
      options.process_filter = Narrow(argv[++index]);
    } else if (arg == "--since" && index + 1 < argc) {
      options.since_filter = Narrow(argv[++index]);
    } else if (arg == "--uia" && index + 1 < argc) {
      options.uia_path = Narrow(argv[++index]);
    } else if (arg == "--format" && index + 1 < argc) {
      options.format = Narrow(argv[++index]);
    } else {
      options.help = true;
    }
  }
  return options;
}

void PrintUsage() {
  std::cout
      << "Usage:\n"
      << "  mwime_hostdiag_summarize.exe --log <debug.log> "
         "[--process name] [--since yyyy-mm-dd] [--uia file] "
         "[--format text|csv]\n"
      << "  mwime_hostdiag_summarize.exe --self-test\n";
}

bool Contains(const std::string& text, const std::string& value) {
  return text.find(value) != std::string::npos;
}

std::string Field(const std::string& line, const std::string& key) {
  const std::string prefix = key + "=";
  const std::size_t start = line.find(prefix);
  if (start == std::string::npos) {
    return {};
  }
  const std::size_t value_start = start + prefix.size();
  if (value_start < line.size() && line[value_start] == '"') {
    const std::size_t end_quote = line.find('"', value_start + 1);
    if (end_quote == std::string::npos) {
      return line.substr(value_start + 1);
    }
    return line.substr(value_start + 1, end_quote - value_start - 1);
  }
  const std::size_t end = line.find(' ', value_start);
  return line.substr(value_start,
                     end == std::string::npos ? std::string::npos
                                              : end - value_start);
}

std::uint64_t ParseUint64(const std::string& text) {
  if (text.empty()) {
    return 0;
  }
  return static_cast<std::uint64_t>(_strtoui64(text.c_str(), nullptr, 10));
}

struct SnapshotRecord {
  std::string stage;
  std::string process;
  std::uint64_t key_path_id = 0;
  std::string context;
  std::string document_mgr;
  std::string top_context;
  std::string view_hwnd;
  std::string foreground_hwnd;
  std::string focus_hwnd;
  std::string caret_hwnd;
  std::string view_class;
  std::string foreground_class;
  std::string focus_class;
  std::string caret_class;
  std::string top_level_class;
  bool transitory = false;
  bool cuas_emulated = false;
  bool full_context = false;
};

struct WriteRecord {
  std::string stage;
  std::uint64_t key_path_id = 0;
  std::string hr;
  std::string request_hr;
  std::string session_hr;
  std::string operation_types;
  std::string planned_action_types;
  std::string request_flags;
  std::string text_utf16_length;
};

struct ClassificationRecord {
  std::string stage;
  std::uint64_t key_path_id = 0;
  std::string classification;
  std::string write_hr;
};

struct ProcessSummary {
  std::string process;
  std::vector<SnapshotRecord> snapshots;
  std::vector<WriteRecord> writes;
  std::vector<ClassificationRecord> classifications;
};

struct UiaSummary {
  bool present = false;
  std::map<std::string, std::string> fields;
};

bool LineAllowed(const std::string& line, const Options& options) {
  if (!options.since_filter.empty() &&
      line.find("[" + options.since_filter) == std::string::npos &&
      line.substr(0, options.since_filter.size()) < options.since_filter) {
    return false;
  }
  if (!options.process_filter.empty() &&
      line.find("process=" + options.process_filter) == std::string::npos) {
    return false;
  }
  return true;
}

void ParseLogLine(const std::string& line, const Options& options,
                  std::map<std::string, ProcessSummary>* summaries) {
  if (summaries == nullptr || !LineAllowed(line, options)) {
    return;
  }
  if (!Contains(line, "[HostDiagnostics]")) {
    return;
  }

  std::string process = Field(line, "process");
  if (process.empty()) {
    process = options.process_filter.empty() ? "<unknown>" : options.process_filter;
  }
  ProcessSummary& summary = (*summaries)[process];
  summary.process = process;

  if (Contains(line, "[Snapshot]")) {
    SnapshotRecord record;
    record.stage = Field(line, "stage");
    record.process = process;
    record.key_path_id = ParseUint64(Field(line, "key_path_id"));
    record.context = Field(line, "context");
    record.document_mgr = Field(line, "document_mgr");
    record.top_context = Field(line, "top_context");
    record.view_hwnd = Field(line, "view_hwnd");
    record.foreground_hwnd = Field(line, "foreground_hwnd");
    record.focus_hwnd = Field(line, "focus_hwnd");
    record.caret_hwnd = Field(line, "caret_hwnd");
    record.view_class = Field(line, "view_class");
    record.foreground_class = Field(line, "foreground_class");
    record.focus_class = Field(line, "focus_class");
    record.caret_class = Field(line, "caret_class");
    record.top_level_class = Field(line, "top_level_class");
    record.transitory = Field(line, "transitory") == "1";
    record.cuas_emulated = Field(line, "cuas_emulated") == "1";
    record.full_context = Field(line, "full_context") == "1";
    summary.snapshots.push_back(std::move(record));
  } else if (Contains(line, "[WriteProbeEx]") ||
             Contains(line, "[WriteProbe]")) {
    WriteRecord record;
    record.stage = Field(line, "stage");
    record.key_path_id = ParseUint64(Field(line, "key_path_id"));
    record.hr = Field(line, "hr");
    record.request_hr = Field(line, "request_hr");
    record.session_hr = Field(line, "session_hr");
    record.operation_types = Field(line, "operation_types");
    record.planned_action_types = Field(line, "planned_action_types");
    record.request_flags = Field(line, "request_flags");
    record.text_utf16_length = Field(line, "text_utf16_length");
    summary.writes.push_back(std::move(record));
  } else if (Contains(line, "[Classify]")) {
    ClassificationRecord record;
    record.stage = Field(line, "stage");
    record.key_path_id = ParseUint64(Field(line, "key_path_id"));
    record.classification = Field(line, "classification");
    record.write_hr = Field(line, "write_hr");
    summary.classifications.push_back(std::move(record));
  }
}

bool KeyPathUnstable(const std::vector<SnapshotRecord>& records) {
  if (records.size() < 2) {
    return false;
  }
  const SnapshotRecord& first = records.front();
  for (const SnapshotRecord& record : records) {
    if (!first.context.empty() && !record.context.empty() &&
        first.context != record.context) {
      return true;
    }
    if (!first.view_hwnd.empty() && !record.view_hwnd.empty() &&
        first.view_hwnd != record.view_hwnd) {
      return true;
    }
    if (!first.focus_hwnd.empty() && !record.focus_hwnd.empty() &&
        first.focus_hwnd != record.focus_hwnd) {
      return true;
    }
    if (!first.caret_hwnd.empty() && !record.caret_hwnd.empty() &&
        first.caret_hwnd != record.caret_hwnd) {
      return true;
    }
  }
  return false;
}

std::map<std::uint64_t, std::vector<SnapshotRecord>> GroupByKeyPath(
    const std::vector<SnapshotRecord>& snapshots) {
  std::map<std::uint64_t, std::vector<SnapshotRecord>> groups;
  for (const SnapshotRecord& snapshot : snapshots) {
    if (snapshot.key_path_id != 0) {
      groups[snapshot.key_path_id].push_back(snapshot);
    }
  }
  return groups;
}

std::string JoinSet(const std::set<std::string>& values) {
  std::string joined;
  for (const std::string& value : values) {
    if (value.empty()) {
      continue;
    }
    if (!joined.empty()) {
      joined += ",";
    }
    joined += value;
  }
  return joined;
}

std::string SurfaceFamily(const ProcessSummary& summary,
                          const UiaSummary& uia);
std::string WriteCapability(const std::string& surface_family,
                            const UiaSummary& uia);
std::string ExcludedReason(const std::string& surface_family);

void PrintTextSummary(const ProcessSummary& summary, const UiaSummary& uia) {
  std::set<std::string> stages;
  std::set<std::string> classes;
  std::set<std::string> classifications;
  for (const SnapshotRecord& snapshot : summary.snapshots) {
    stages.insert(snapshot.stage);
    classes.insert(snapshot.view_class);
    classes.insert(snapshot.focus_class);
    classes.insert(snapshot.top_level_class);
  }
  for (const ClassificationRecord& record : summary.classifications) {
    classifications.insert(record.classification);
  }

  const auto key_paths = GroupByKeyPath(summary.snapshots);
  std::size_t unstable_paths = 0;
  for (const auto& [id, records] : key_paths) {
    if (KeyPathUnstable(records)) {
      ++unstable_paths;
    }
  }

  std::cout << "process=" << summary.process << "\n";
  std::cout << "  snapshots=" << summary.snapshots.size()
            << " writes=" << summary.writes.size()
            << " classifications=" << summary.classifications.size()
            << " key_paths=" << key_paths.size()
            << " unstable_key_paths=" << unstable_paths << "\n";
  std::cout << "  stages=" << JoinSet(stages) << "\n";
  std::cout << "  classes=" << JoinSet(classes) << "\n";
  std::cout << "  classifications=" << JoinSet(classifications) << "\n";
  const std::string surface_family = SurfaceFamily(summary, uia);
  const std::string write_capability = WriteCapability(surface_family, uia);
  std::cout << "  surface_family=" << surface_family << "\n";
  std::cout << "  write_capability=" << write_capability << "\n";
  std::cout << "  excluded_reason=" << ExcludedReason(surface_family) << "\n";
  for (const WriteRecord& write : summary.writes) {
    std::cout << "  write stage=" << write.stage
              << " key_path_id=" << write.key_path_id
              << " hr=" << write.hr
              << " request_hr=" << write.request_hr
              << " session_hr=" << write.session_hr
              << " operations=" << write.operation_types
              << " plan=" << write.planned_action_types << "\n";
  }
}

void PrintCsvHeader() {
  std::cout << "process,snapshots,writes,classifications,key_paths,"
               "unstable_key_paths,classes,classifications_seen\n";
}

void PrintCsvSummary(const ProcessSummary& summary) {
  std::set<std::string> classes;
  std::set<std::string> classifications;
  for (const SnapshotRecord& snapshot : summary.snapshots) {
    classes.insert(snapshot.view_class);
    classes.insert(snapshot.focus_class);
    classes.insert(snapshot.top_level_class);
  }
  for (const ClassificationRecord& record : summary.classifications) {
    classifications.insert(record.classification);
  }
  const auto key_paths = GroupByKeyPath(summary.snapshots);
  std::size_t unstable_paths = 0;
  for (const auto& [id, records] : key_paths) {
    if (KeyPathUnstable(records)) {
      ++unstable_paths;
    }
  }
  std::cout << summary.process << "," << summary.snapshots.size() << ","
            << summary.writes.size() << ","
            << summary.classifications.size() << "," << key_paths.size()
            << "," << unstable_paths << ",\"" << JoinSet(classes)
            << "\",\"" << JoinSet(classifications) << "\"\n";
}

bool LoadLog(const std::string& path, const Options& options,
             std::map<std::string, ProcessSummary>* summaries) {
  std::ifstream input(path);
  if (!input) {
    return false;
  }
  std::string line;
  while (std::getline(input, line)) {
    ParseLogLine(line, options, summaries);
  }
  return true;
}

UiaSummary LoadUiaSummary(const std::string& path) {
  UiaSummary summary;
  if (path.empty()) {
    return summary;
  }
  std::ifstream input(path);
  if (!input) {
    return summary;
  }
  summary.present = true;
  std::string line;
  while (std::getline(input, line)) {
    const std::vector<std::string> keys = {
        "framework_id", "control_type", "class_name", "automation_id",
        "native_hwnd", "native_class", "value_pattern_hr", "supported",
        "readonly", "value_set_hr", "value_requested_match",
        "text_pattern_hr", "legacy_iaccessible_pattern_hr"};
    for (const std::string& key : keys) {
      const std::string value = Field(line, key);
      if (!value.empty()) {
        summary.fields[key] = value;
      }
    }
  }
  return summary;
}

std::string UiaField(const UiaSummary& summary, const std::string& key) {
  const auto found = summary.fields.find(key);
  return found == summary.fields.end() ? "" : found->second;
}

std::string LowerAscii(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return text;
}

std::string NormalizeClass(std::string class_name) {
  if (class_name.rfind("Avalonia-", 0) == 0) {
    return "Avalonia";
  }
  return class_name;
}

bool SummaryHasClass(const ProcessSummary& summary,
                     const std::string& class_name) {
  for (const SnapshotRecord& snapshot : summary.snapshots) {
    if (NormalizeClass(snapshot.view_class) == class_name ||
        NormalizeClass(snapshot.focus_class) == class_name ||
        NormalizeClass(snapshot.top_level_class) == class_name) {
      return true;
    }
  }
  return false;
}

bool SummaryHasViewClass(const ProcessSummary& summary,
                         const std::string& class_name) {
  for (const SnapshotRecord& snapshot : summary.snapshots) {
    if (NormalizeClass(snapshot.view_class) == class_name) {
      return true;
    }
  }
  return false;
}

bool HasUiaSearchBoxIdentity(const UiaSummary& uia) {
  return UiaField(uia, "framework_id") == "Avalonia" &&
         UiaField(uia, "control_type") == "Edit" &&
         UiaField(uia, "automation_id") == "SearchTextBox" &&
         UiaField(uia, "class_name") == "CustomTextBox" &&
         UiaField(uia, "readonly") == "false";
}

std::string SurfaceFamily(const ProcessSummary& summary,
                          const UiaSummary& uia) {
  if (UiaField(uia, "control_type") == "Window") {
    return "window_only";
  }
  if (SummaryHasClass(summary, "UnityWndClass") ||
      SummaryHasClass(summary, "org.wezfurlong.wezterm")) {
    return "window_only";
  }

  if (SummaryHasViewClass(summary, "RICHEDIT50W") ||
      UiaField(uia, "native_class") == "RICHEDIT50W") {
    return "native_richedit";
  }
  if (SummaryHasViewClass(summary, "Edit") ||
      UiaField(uia, "native_class") == "Edit") {
    return "native_edit";
  }
  if (HasUiaSearchBoxIdentity(uia) || SummaryHasClass(summary, "Avalonia")) {
    return "framework_avalonia";
  }

  return "unknown";
}

std::string WriteCapability(const std::string& surface_family,
                            const UiaSummary& uia) {
  if (surface_family == "native_richedit" || surface_family == "native_edit") {
    return "native_selection_replace";
  }
  if (surface_family == "framework_avalonia" && HasUiaSearchBoxIdentity(uia)) {
    return "uia_avalonia_searchbox_whole_value_replace";
  }
  if (surface_family == "window_only") {
    return "none";
  }
  return "unknown";
}

std::string ExcludedReason(const std::string& surface_family) {
  if (surface_family == "window_only") {
    return "window_only_not_text_surface";
  }
  return "";
}

void PrintUiaTextSummary(const UiaSummary& summary) {
  if (!summary.present) {
    return;
  }
  std::cout << "uia:\n";
  std::cout << "  framework_id=" << UiaField(summary, "framework_id")
            << " control_type=" << UiaField(summary, "control_type")
            << " class_name=" << UiaField(summary, "class_name")
            << " automation_id=" << UiaField(summary, "automation_id")
            << "\n";
  std::cout << "  native_hwnd=" << UiaField(summary, "native_hwnd")
            << " native_class=" << UiaField(summary, "native_class")
            << " value_pattern_hr=" << UiaField(summary, "value_pattern_hr")
            << " readonly=" << UiaField(summary, "readonly")
            << " value_set_hr=" << UiaField(summary, "value_set_hr")
            << " value_requested_match="
            << UiaField(summary, "value_requested_match") << "\n";
}

bool SelfTest() {
  const Options options;
  std::map<std::string, ProcessSummary> summaries;
  ParseLogLine(
      "[2026-05-20][pid=1] [HostDiagnostics][Snapshot] stage=OnTestKeyDown "
      "key_path_id=7 process=FluentSearch.exe context=0x1 document_mgr=0x2 "
      "top_context=0x1 transitory=1 cuas_emulated=0 full_context=1 "
      "view_hwnd=0x3 view_class=Avalonia-x focus_hwnd=0x3 "
      "focus_class=Avalonia-x caret_hwnd=0x3 caret_class=Avalonia-x "
      "top_level_class=Avalonia-x",
      options, &summaries);
  ParseLogLine(
      "[2026-05-20][pid=1] [HostDiagnostics][Snapshot] stage=OnKeyDown.Begin "
      "key_path_id=7 process=FluentSearch.exe context=0x1 document_mgr=0x2 "
      "top_context=0x1 transitory=1 cuas_emulated=0 full_context=1 "
      "view_hwnd=0x3 view_class=Avalonia-x focus_hwnd=0x3 "
      "focus_class=Avalonia-x caret_hwnd=0x3 caret_class=Avalonia-x "
      "top_level_class=Avalonia-x",
      options, &summaries);
  ParseLogLine(
      "[2026-05-20][pid=1] [HostDiagnostics][WriteProbeEx] "
      "stage=RequestEditSession key_path_id=7 process=FluentSearch.exe "
      "text_utf16_length=1 request_hr=0x00000000 session_hr=0x00000000 "
      "request_flags=0x00000002 operation_types=StartComposition "
      "planned_action_types=StartComposition",
      options, &summaries);
  ParseLogLine(
      "[2026-05-20][pid=1] [HostDiagnostics][Classify] "
      "stage=OnKeyDown.AfterFlush key_path_id=7 process=FluentSearch.exe "
      "classification=stable_target write_hr=0x00000000",
      options, &summaries);

  const auto found = summaries.find("FluentSearch.exe");
  if (found == summaries.end()) {
    return false;
  }
  const ProcessSummary& summary = found->second;
  if (summary.snapshots.size() != 2 || summary.writes.size() != 1 ||
      summary.classifications.size() != 1) {
    return false;
  }
  const auto groups = GroupByKeyPath(summary.snapshots);
  if (groups.size() != 1 || groups.begin()->first != 7 ||
      KeyPathUnstable(groups.begin()->second)) {
    return false;
  }
  if (summary.writes.front().operation_types != "StartComposition") {
    return false;
  }

  UiaSummary fluent_uia;
  fluent_uia.present = true;
  fluent_uia.fields["framework_id"] = "Avalonia";
  fluent_uia.fields["control_type"] = "Edit";
  fluent_uia.fields["class_name"] = "CustomTextBox";
  fluent_uia.fields["automation_id"] = "SearchTextBox";
  fluent_uia.fields["readonly"] = "false";
  if (SurfaceFamily(summary, fluent_uia) != "framework_avalonia") {
    return false;
  }
  if (WriteCapability("framework_avalonia", fluent_uia) !=
      "uia_avalonia_searchbox_whole_value_replace") {
    return false;
  }

  ProcessSummary wezterm;
  wezterm.process = "wezterm-gui.exe";
  SnapshotRecord wezterm_snapshot;
  wezterm_snapshot.view_class = "org.wezfurlong.wezterm";
  wezterm_snapshot.focus_class = "org.wezfurlong.wezterm";
  wezterm_snapshot.top_level_class = "org.wezfurlong.wezterm";
  wezterm.snapshots.push_back(wezterm_snapshot);
  if (SurfaceFamily(wezterm, UiaSummary{}) != "window_only") {
    return false;
  }

  ProcessSummary rich_edit;
  rich_edit.process = "KakaoTalk.exe";
  SnapshotRecord rich_edit_snapshot;
  rich_edit_snapshot.view_class = "RICHEDIT50W";
  rich_edit.snapshots.push_back(rich_edit_snapshot);
  if (SurfaceFamily(rich_edit, UiaSummary{}) != "native_richedit") {
    return false;
  }
  if (WriteCapability("native_richedit", UiaSummary{}) !=
      "native_selection_replace") {
    return false;
  }

  return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  const Options options = ParseArgs(argc, argv);
  if (options.help) {
    PrintUsage();
    return 1;
  }
  if (options.self_test) {
    if (!SelfTest()) {
      std::cerr << "self-test failed\n";
      return 2;
    }
    std::cout << "self-test passed\n";
    return 0;
  }
  if (options.log_path.empty()) {
    PrintUsage();
    return 1;
  }

  std::map<std::string, ProcessSummary> summaries;
  if (!LoadLog(options.log_path, options, &summaries)) {
    std::cerr << "failed to read log: " << options.log_path << "\n";
    return 3;
  }
  const UiaSummary uia_summary = LoadUiaSummary(options.uia_path);

  if (options.format == "csv") {
    PrintCsvHeader();
    for (const auto& [process, summary] : summaries) {
      PrintCsvSummary(summary);
    }
  } else {
    PrintUiaTextSummary(uia_summary);
    for (const auto& [process, summary] : summaries) {
      PrintTextSummary(summary, uia_summary);
    }
  }
  return 0;
}
