#pragma once

#include <cstdint>
#include <functional>
#include <queue>
#include <glog/logging.h>
#include <folly/SharedMutex.h>
#include <folly/experimental/io/AsyncBase.h>
#include <folly/experimental/io/Libxnvme.h>

#ifdef FOLLY_HAS_LIBXNVME

#include <libxnvme.h>

namespace folly {
enum command_type { PREAD, PWRITE, PREADV, PWRITEV, CMD_PASS, CMD_PASS_ADMIN };
typedef void (*xnvme_cmd_setting_fn)(xnvme_spec_cmd);

struct XNMeOpArgs {
  command_type cmd_type;
  int fd;
  off_t start;

  union {
    // For single buffer operations (pread and pwrite)
    struct {
      void* buf;
      size_t size;
    } single_buffer;

    // For vectorized operations (preadv and pwritev)
    struct {
      const iovec* iov;
      int iovcnt;
    } vector_buffer;

    // For cmd_pass
    struct {
      void* dbuf;
      size_t dbuf_nbytes;
      void* mbuf;
      size_t mbuf_nbytes;
      xnvme_cmd_setting_fn fn;
    } cmd_buffers;
  } operation;
};

inline size_t total_iov_size(XNMeOpArgs& args) {
  size_t iov_size = 0;
  for (int i = 0; i < args.operation.vector_buffer.iovcnt; i++) {
    iov_size += args.operation.vector_buffer.iov[i].iov_len;
  }
  return iov_size;
}

class XNVMeOp : public AsyncBaseOp {
  friend class XNVMe;

 public:
  uint64_t cdw{0};
  XNVMeOp(NotificationCallback cb = NotificationCallback()) {};
  XNVMeOp(const XNVMeOp&) = delete;
  XNVMeOp& operator=(const XNVMeOp&) = delete;
  ~XNVMeOp() override {};

  AsyncIOOp* getAsyncIOOp() override { return nullptr; }
  IoUringOp* getIoUringOp() override { return nullptr; }
  XNVMeOp* getXNVMeOp() override { return this; }

  void reset(NotificationCallback cb) override;

  void toStream(std::ostream& os) const override;

  // API introduced to open a device handle
  void pread(int fd, void* buf, size_t size, off_t start) override;
  void preadv(int fd, const iovec* iov, int iovcnt, off_t start) override;
  void pwrite(int fd, const void* buf, size_t size, off_t start) override;
  void pwritev(int fd, const iovec* iov, int iovcnt, off_t start) override;

  // API introduced to handle the passing of commands
  void cmd_pass(
      void* dbuf,
      size_t dbuf_nbytes,
      void* mbuf,
      size_t mbuf_nbytes,
      xnvme_cmd_setting_fn fn);

  void cmd_pass_admin(
      void* dbuf,
      size_t dbuf_nbytes,
      void* mbuf,
      size_t mbuf_nbytes,
      xnvme_cmd_setting_fn fn);

  XNMeOpArgs args;
};

class XNVMe : public AsyncBase {
 public:
  using Op = XNVMeOp;
  constexpr static std::chrono::duration<double> defaultPollingInterval =
      std::chrono::microseconds(10);

  explicit XNVMe(
      size_t capacity,
      std::string& device_uri,
      xnvme_opts opts = xnvme_opts_default(),
      PollMode pollMode = POLLABLE,
      std::chrono::duration<double> sleepWhilePolling = defaultPollingInterval);

  XNVMe(const XNVMe&) = delete;
  XNVMe& operator=(const XNVMe&) = delete;
  ~XNVMe() override;
  bool isAvailable() { return available.load(); }
  void process_fn(struct xnvme_cmd_ctx* ctx, XNVMeOp* op);

 protected:
  int submitOne(AsyncBaseOp* Op) override;
  int submitRange(Range<AsyncBaseOp**> ops) override;
  void initializeContext() override;
  int drainPollFd() override;

 private:
  struct xnvme_dev* device = nullptr;
  struct xnvme_opts opts_;
  struct xnvme_queue* queue_ = nullptr;
  mutable SharedMutex singleMutex_;
  std::chrono::duration<double> sleepIntervalWhilePolling_{
      defaultPollingInterval};

  std::atomic<bool> available{false};
  std::vector<XNVMeOp*> results;

  Range<AsyncBase::Op**> doWait(
      WaitType type,
      size_t minRequests,
      size_t maxRequests,
      std::vector<AsyncBase::Op*>& result) override;

  int parseAndCmdPass(XNVMeOp* op);
};

struct xnvme_callback_args {
  struct XNVMeOp* op;
  struct XNVMe* backend;

  xnvme_callback_args(XNVMeOp* the_op, XNVMe* the_backend)
      : op(the_op), backend(the_backend) {}
};
} // namespace folly
#endif
