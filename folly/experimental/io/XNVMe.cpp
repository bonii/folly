#include "XNVMe.h"
#include <folly/experimental/io/XNVMe.h>

#if FOLLY_HAS_LIBXNVME

namespace folly {

void XNVMeOp::pread(int fd, void* buf, size_t size, off_t start) {
  xnvme_file_pread(
      &(xnvme_file_get_cmd_ctx(getDeviceHandle(fd))), buf, size, start);
}

void XNVMeOp::pwrite(int fd, const void* buf, size_t size, off_t start) {
 xnvme_file_pwrite(&(xnvme_file_get_cmd_ctx(getDeviceHandle(fd))), const_cast<void*>(buf), size, start); 
}

// Maps an open file descriptor to a device handle
xnvme_dev* getDeviceHandle(int fd) {
  // TODO: Implement
  return (xnvme_dev*)nullptr;
}

XNVMe::XNVMe(
    size_t capacity,
    folly::AsyncBase::PollMode pollMode,
    std::string& device_uri,
    xnvme_opts opts = xnvme_opts_default())
    : AsyncBase(capacity, pollMode), opts_(opts) {
  // Need to do some error checking here
  device = xnvme_dev_open(device_uri.c_str(), &opts_);
  xnvme_queue_init(device, capacity, 0, &queue_);
}

XNVMe::~XNVMe() {
  // TODO Implement
  xnvme_queue_term(queue_);
  xnvme_dev_close(device);
}

int XNVMe::submitOne(AsyncBaseOp* op) {
  if(!op) {
    return -1;
  }

  std::unique_lock lk(submitMutex_);
  
  return 0;
}

int XNVMe::submitRange(Range<AsyncBaseOp**> ops) {
  return 0;
}
} // namespace folly

#endif
