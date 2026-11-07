#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <msctf.h>

#include <vector>
#include <memory>

#include "tsf/edit/text_edit_sink.h"

namespace milkyway::tsf::service {
class TipTextService;
}

namespace milkyway::tsf::edit {

struct WriteSessionState;

class TsfTextEditSink final : public TextEditSink {
 public:
  explicit TsfTextEditSink(service::TipTextService* host);
  ~TsfTextEditSink() override;

  void Apply(const TextEditOperation& operation) override;

  bool HasPendingOperations() const;
  std::size_t PendingOperationCount() const;
  bool is_flushing() const;
  void ClearPendingOperations();
  void InvalidatePendingEdits();
  HRESULT Flush(ITfContext* context, DWORD request_flags);

 private:
  service::TipTextService* host_ = nullptr;
  std::vector<TextEditOperation> pending_operations_;
  std::shared_ptr<WriteSessionState> state_;
};

}  // namespace milkyway::tsf::edit

#endif
