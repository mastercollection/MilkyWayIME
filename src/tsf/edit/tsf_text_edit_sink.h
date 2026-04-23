#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <msctf.h>

#include <vector>

#include "tsf/edit/text_edit_sink.h"

namespace milkyway::tsf::service {
class TipTextService;
}

namespace milkyway::tsf::edit {

class TsfTextEditSink final : public TextEditSink {
 public:
  explicit TsfTextEditSink(service::TipTextService* host);

  void Apply(const TextEditOperation& operation) override;

  bool HasPendingOperations() const;
  std::size_t PendingOperationCount() const;
  bool is_flushing() const;
  void ClearPendingOperations();
  HRESULT Flush(ITfContext* context, DWORD request_flags);

 private:
  service::TipTextService* host_ = nullptr;
  std::vector<TextEditOperation> pending_operations_;
  bool flushing_ = false;
};

}  // namespace milkyway::tsf::edit

#endif
