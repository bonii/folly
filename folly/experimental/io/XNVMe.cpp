#include "XNVMe.h"

#include <bit>
#include <iostream>
#include <folly/experimental/io/XNVMe.h>

#if FOLLY_HAS_LIBXNVME

namespace folly {

void XNVMeOp::reset(NotificationCallback cb) {
  CHECK_NE(state_, State::PENDING);
  cb_ = std::move(cb);
  state_ = State::UNINITIALIZED;
  result_ = -EINVAL;
}

void XNVMeOp::pread(int fd, void* buf, size_t size, off_t start) {
  init();
  args.cmd_type = PREAD;
  args.fd = fd;
  args.start = start;

  args.operation.single_buffer.buf = buf;
  args.operation.single_buffer.size = size;
}

void XNVMeOp::pwrite(int fd, const void* buf, size_t size, off_t start) {
  init();
  args.cmd_type = PWRITE;
  args.fd = fd;
  args.start = start;

  args.operation.single_buffer.buf = const_cast<void*>(buf);
  args.operation.single_buffer.size = size;
}

void XNVMeOp::preadv(int fd, const iovec* iov, int iovcnt, off_t start) {
  init();
  args.cmd_type = PREADV;
  args.fd = fd;
  args.start = start;

  args.operation.vector_buffer.iov = iov;
  args.operation.vector_buffer.iovcnt = iovcnt;
}

void XNVMeOp::pwritev(int fd, const iovec* iov, int iovcnt, off_t start) {
  init();
  args.cmd_type = PWRITEV;
  args.fd = fd;
  args.start = start;

  args.operation.vector_buffer.iov = iov;
  args.operation.vector_buffer.iovcnt = iovcnt;
}

void XNVMeOp::cmd_pass(
    void* dbuf,
    size_t dbuf_nbytes,
    void* mbuf,
    size_t mbuf_nbytes,
    xnvme_cmd_setting_fn fn) {
  init();
  args.cmd_type = CMD_PASS;
  args.operation.cmd_buffers.dbuf = dbuf;
  args.operation.cmd_buffers.dbuf_nbytes = dbuf_nbytes;
  args.operation.cmd_buffers.mbuf = mbuf;
  args.operation.cmd_buffers.mbuf_nbytes = mbuf_nbytes;
  args.operation.cmd_buffers.fn = fn;
}

void XNVMeOp::cmd_pass_admin(
    void* dbuf,
    size_t dbuf_nbytes,
    void* mbuf,
    size_t mbuf_nbytes,
    xnvme_cmd_setting_fn fn) {
  init();
  args.cmd_type = CMD_PASS_ADMIN;
  args.operation.cmd_buffers.dbuf = dbuf;
  args.operation.cmd_buffers.dbuf_nbytes = dbuf_nbytes;
  args.operation.cmd_buffers.mbuf = mbuf;
  args.operation.cmd_buffers.mbuf_nbytes = mbuf_nbytes;
  args.operation.cmd_buffers.fn = fn;
}

void XNVMeOp::toStream(std::ostream& os) const {
  // TODO: Implement
}

XNVMe::XNVMe(
    size_t capacity,
    std::string& device_uri,
    xnvme_opts opts,
    folly::AsyncBase::PollMode pollMode,
    std::chrono::duration<double> sleepWhilePolling)
    : AsyncBase(capacity, pollMode),
      opts_(opts),
      sleepIntervalWhilePolling_(sleepWhilePolling) {
  std::unique_lock lk{singleMutex_};
  device = xnvme_dev_open(device_uri.c_str(), &opts_);
  std::cout << opts_.be << "  ,  " << opts_.async << std::endl;
  constexpr size_t MAX_XNVME_QUEUE_CAPACITY = 4096;
  CHECK_LE(capacity, MAX_XNVME_QUEUE_CAPACITY);
  if (device != nullptr) {
    CHECK_ERR(xnvme_dev_derive_geo(device));
    // Round off the capacity to the closest power of two
    capacity = std::__bit_ceil(capacity);
    CHECK_ERR(xnvme_queue_init(device, capacity, 0, &queue_));
    available.store(true);
    initializeContext();
  }
}

XNVMe::~XNVMe() {
  if (available.load()) {
    std::unique_lock lk {singleMutex_};
    CHECK_ERR(xnvme_queue_drain(queue_));
    CHECK_ERR(xnvme_queue_term(queue_));
    if (device != nullptr)
      xnvme_dev_close(device);
  }
}

void XNVMe::process_fn(struct xnvme_cmd_ctx* ctx, XNVMeOp* op) {
  std::unique_lock lk(singleMutex_);
  if (xnvme_cmd_ctx_cpl_status(ctx)) {
    xnvme_cli_pinf("Command did not complete successfully");
    xnvme_cmd_ctx_pr(ctx, XNVME_PR_DEF);
  } else {
    xnvme_cli_pinf("Command completed succesfully");
  }
  op->cdw = ctx->cpl.result;
  decrementPending();
  xnvme_queue_put_cmd_ctx(ctx->async.queue, ctx);  
  results.emplace_back(op);
}

void completion_callback_fn(struct xnvme_cmd_ctx* ctx, void* cb_arg) {
  // Invoke the processing function
  auto args = static_cast<xnvme_callback_args*>(cb_arg);
  args->backend->process_fn(ctx, args->op);
  delete args;
}

int XNVMe::parseAndCmdPass(XNVMeOp* the_op) {
  xnvme_cmd_ctx* cmd_ctx = xnvme_queue_get_cmd_ctx(queue_);
  if (!cmd_ctx)
    return -1;

  cmd_ctx->async.cb = completion_callback_fn;
  cmd_ctx->async.cb_arg =
      static_cast<void*>(new xnvme_callback_args(the_op, this));
  cmd_ctx->cmd.common.nsid = xnvme_dev_get_nsid(cmd_ctx->dev);

  switch (the_op->args.cmd_type) {
    case command_type::PREAD:
      cmd_ctx->cmd.common.opcode = XNVME_SPEC_FS_OPC_READ;
      cmd_ctx->cmd.nvm.slba = the_op->args.start;
      xnvme_cmd_pass(
          cmd_ctx,
          the_op->args.operation.single_buffer.buf,
          the_op->args.operation.single_buffer.size,
          NULL,
          0);
      break;

    case command_type::PWRITE:
      cmd_ctx->cmd.common.opcode = XNVME_SPEC_FS_OPC_WRITE;
      cmd_ctx->cmd.nvm.slba = the_op->args.start;
      xnvme_cmd_pass(
          cmd_ctx,
          the_op->args.operation.single_buffer.buf,
          the_op->args.operation.single_buffer.size,
          NULL,
          0);
      break;

    case command_type::PREADV:
      cmd_ctx->cmd.common.opcode = XNVME_SPEC_FS_OPC_READ;
      cmd_ctx->cmd.nvm.slba = the_op->args.start;
      xnvme_cmd_pass_iov(
          cmd_ctx,
          const_cast<iovec*>(the_op->args.operation.vector_buffer.iov),
          the_op->args.operation.vector_buffer.iovcnt,
          total_iov_size(the_op->args),
          NULL,
          0);
      break;

    case command_type::PWRITEV:
      cmd_ctx->cmd.common.opcode = XNVME_SPEC_FS_OPC_WRITE;
      cmd_ctx->cmd.nvm.slba = the_op->args.start;
      xnvme_cmd_pass_iov(
          cmd_ctx,
          const_cast<iovec*>(the_op->args.operation.vector_buffer.iov),
          the_op->args.operation.vector_buffer.iovcnt,
          total_iov_size(the_op->args),
          NULL,
          0);
      break;

    case command_type::CMD_PASS:
      // Invoke the function to set the appropriate nvme command
      the_op->args.operation.cmd_buffers.fn(cmd_ctx->cmd);
      xnvme_cmd_pass(
          cmd_ctx,
          the_op->args.operation.cmd_buffers.dbuf,
          the_op->args.operation.cmd_buffers.dbuf_nbytes,
          the_op->args.operation.cmd_buffers.mbuf,
          the_op->args.operation.cmd_buffers.mbuf_nbytes);
      break;

    case command_type::CMD_PASS_ADMIN:
      // Invoke the function to set the appropriate nvme command
      the_op->args.operation.cmd_buffers.fn(cmd_ctx->cmd);
      xnvme_cmd_pass_admin(
          cmd_ctx,
          the_op->args.operation.cmd_buffers.dbuf,
          the_op->args.operation.cmd_buffers.dbuf_nbytes,
          the_op->args.operation.cmd_buffers.mbuf,
          the_op->args.operation.cmd_buffers.mbuf_nbytes);
      break;

    default:
      return -1;
  }
  return 1;
}

int XNVMe::submitOne(AsyncBaseOp* op) {
  if (!op) {
    return -1;
  }
  CHECK(queue_);
  auto the_op = op->getXNVMeOp();
  std::unique_lock lk(singleMutex_);
  return parseAndCmdPass(the_op);
}

int XNVMe::submitRange(Range<AsyncBaseOp**> ops) {
  size_t total = 0;
  std::unique_lock lk(singleMutex_);

  for (size_t i = 0; i < ops.size(); i++) {
    if (parseAndCmdPass(ops[i]->getXNVMeOp()))
      total++;
  }

  return total > 0 ? total : -1;
}

void XNVMe::initializeContext() {
  if (!init_.load(std::memory_order_acquire)) {
    std::lock_guard<std::mutex> lock(initMutex_);
    if (!init_.load(std::memory_order_relaxed)) {
      init_.store(true, std::memory_order_release);
    }
  }
}

int XNVMe::drainPollFd() {
  // TODO Implement
  return -1;
}

Range<AsyncBase::Op**> XNVMe::doWait(
    WaitType type,
    size_t minRequests,
    size_t maxRequests,
    std::vector<AsyncBase::Op*>& result) {
  result.clear();

  size_t outstanding = maxRequests;
  size_t completed = 0;
  size_t total_completed = 0;
  while (completed < minRequests) {
    // Spin continuously for requests with a configured backoff
    if (pollMode_ == POLLABLE)
      std::this_thread::sleep_for(sleepIntervalWhilePolling_);
    completed = xnvme_queue_poke(queue_, maxRequests - total_completed);
    if (completed > 0)
      total_completed += completed;

    if (completed < 0)
      throw std::runtime_error("Something wrong");
  }

  std::unique_lock lk(singleMutex_);
  for (auto completed_op : results) {
    CHECK(completed_op);
    // decrementPending();
    switch (type) {
      case WaitType::COMPLETE:
        completed_op->complete(completed_op->cdw);
        break;
      case WaitType::CANCEL:
        completed_op->cancel();
        break;
    }
    result.push_back(std::move(completed_op));
  }
  return range(result);
}
} // namespace folly

#endif
