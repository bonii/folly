#pragma once

#include <folly/SharedMutex.h>
#include <folly/experimental/io/AsyncBase.h>
#include <folly/experimental/io/Libxnvme.h>

#if FOLLY_HAS_LIBXNVME

#include <libxnvme.h>

namespace folly {
class XNVMeOp : public AsyncBaseOp {
 public:
  XNVMeOp(const XNVMeOp&) = delete;
  XNVMeOp& operator=(const XNVMeOp&) = delete;
  ~XNVMeOp() override;

  void pread(int fd, void* buf, size_t size, off_t start) override;
  void preadv(int fd, const iovec* iov, int iovcnt, off_t start) override;
  void pwrite(int fd, const void* buf, size_t size, off_t start) override;
  void pwritev(int fd, const iovec* iov, int iovcnt, off_t start) override;

 protected:
  xnvme_dev* getDeviceHandle(int fd);
};

class XNVMe : public AsyncBase {
 public:
  using Op = XNVMeOp;

  explicit XNVMe(
      size_t capacity,
      PollMode pollMode,
      std::string& device_uri,
      xnvme_opts opts);
  XNVMe(const XNVMe&) = delete;
  XNVMe& operator=(const XNVMe&) = delete;
  ~XNVMe() override;

 protected:
  int submitOne(AsyncBaseOp* Op) override;
  int submitRange(Range<AsyncBaseOp**> ops) override;

 private:
  struct xnvme_dev* device = nullptr;
  struct xnvme_opts opts_;
  struct xnvme_queue* queue_ = nullptr;
  mutable SharedMutex submitMutex_;
};
} // namespace folly
#endif