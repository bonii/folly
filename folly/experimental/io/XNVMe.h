#pragma once

#include <cstdint>
#include <unordered_map>
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

  int openDevice(
      const std::string deviceName, xnvme_opts opts = xnvme_opts_default());
  void closeDevice(const std::string deviceName);
  // API introduced to open a device handle
  void pread(int fd, void* buf, size_t size, off_t start) override;
  void preadv(int fd, const iovec* iov, int iovcnt, off_t start) override;
  void pwrite(int fd, const void* buf, size_t size, off_t start) override;
  void pwritev(int fd, const iovec* iov, int iovcnt, off_t start) override;
  XNVMeOp* getXNVMeOp() override { return this; };

 private:
  xnvme_dev* getDeviceHandle(int fd);
  std::unordered_map<std::string, int> deviceNamesToFileDescriptors;
  std::unordered_map<int, xnvme_dev*> fileDescriptorsToDeviceHandles;

  int allocateFileDescriptor();
  void deallocateFileDescriptor(int);
};

/**
 * Encodes basic NVM operations supported by XNVMe
 */
class XNVMeNVMOp : public XNVMeOp {
  int nvmCompare(
      std::uint32_t nsid,
      std::uint64_t slba,
      std::uint16_t nlb,
      void* dbuf,
      void* mbuf);
  int nvmDsm(
      std::uint32_t nsid,
      struct xnvme_spec_dsm_range* dsm_range,
      std::uint8_t nr,
      bool ad,
      bool idw,
      bool idr);
  int nvmMgmtRecv(
      std::uint32_t nsid,
      std::uint8_t mo,
      std::uint16_t mos,
      void* dbuf,
      std::uint32_t dbuf_nbytes);
  int nvmMgmtSend(
      std::uint32_t nsid,
      std::uint8_t mo,
      std::uint16_t mos,
      void* dbuf,
      std::uint32_t dbuf_nbytes);
  int nvmRead(
      std::uint32_t nsid,
      std::uint64_t slba,
      std::uint16_t nlb,
      void* dbuf,
      void* mbuf);
  int nvmScopy(
      std::uint32_t nsid,
      std::uint64_t sdlba,
      struct xnvme_spec_nvm_scopy_fmt_zero* ranges,
      std::uint8_t nr,
      enum xnvme_nvm_scopy_fmt copy_fmt);
  int nvmWrite(
      std::uint32_t nsid,
      std::uint64_t slba,
      std::uint16_t nlb,
      const void* dbuf,
      const void* mbuf);
  int nvmWriteUncorrectable(
      std::uint32_t nsid, std::uint64_t slba, std::uint16_t nlb);
  int nvmWriteZeroes(std::uint32_t nsid, std::uint64_t slba, std::uint16_t nlb);
  void prepNvm(
      std::uint8_t opcode,
      std::uint32_t nsid,
      std::uint64_t slba,
      std::uint16_t nlb);
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
