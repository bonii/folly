#pragma once

#include <folly/SharedMutex.h>
#include <folly/experimental/io/AsyncBase.h>
#include <folly/experimental/io/Libxnvme.h>
#include <unordered_map>

#if FOLLY_HAS_LIBXNVME

#include <libxnvme.h>

namespace folly {
class XNVMeOp : public AsyncBaseOp {
 public:
  XNVMeOp(const XNVMeOp&) = delete;
  XNVMeOp& operator=(const XNVMeOp&) = delete;
  ~XNVMeOp() override;

  int openDevice(
      const std::string deviceName, xnvme_opts opts = xnvme_opts_default());
  void closeDevice(const std::string deviceName);
  // API introduced to open a device handle
  void pread(int fd, void* buf, size_t size, off_t start) override;
  void preadv(int fd, const iovec* iov, int iovcnt, off_t start) override;
  void pwrite(int fd, const void* buf, size_t size, off_t start) override;
  void pwritev(int fd, const iovec* iov, int iovcnt, off_t start) override;
  XNVMeOp* getXNVMeOp() override { return this; };

 protected:
  std::unordered_map<std::string, int> deviceNamesToFileDescriptors;
  std::unordered_map<int, xnvme_dev*> fileDescriptorsToDeviceHandles;

 private:
  xnvme_dev* getDeviceHandle(int fd);
  int allocateFileDescriptor();
  void deallocateFileDescriptor(int);
};

class XNVMeNVMOp : public XNVMeOp {};

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