#include "XNVMe.h"
#include <folly/experimental/io/XNVMe.h>

#if FOLLY_HAS_LIBXNVME

namespace folly {

int XNVMeOp::openDevice(const std::string deviceName, xnvme_opts opts) {
  auto iter = deviceNamesToFileDescriptors.find(deviceName);
  int fd = -1;
  if (iter != deviceNamesToFileDescriptors.end()) {
    fd = allocateFileDescriptor();
    auto dev = xnvme_dev_open(deviceName.c_str(), &opts);
    if (!dev) {
      auto fd = allocateFileDescriptor();
      fileDescriptorsToDeviceHandles.insert({fd, dev});
      deviceNamesToFileDescriptors.insert({deviceName, fd});
    } else {
      fd = -1;
      // TODO: Better error handling
    }
  }
  return fd;
}

void XNVMeOp::closeDevice(const std::string deviceName) {
  auto iter = deviceNamesToFileDescriptors.find(deviceName);
  if (iter != deviceNamesToFileDescriptors.end()) {
    auto fd = iter->second;
    auto dev = fileDescriptorsToDeviceHandles.find(fd)->second;
    xnvme_dev_close(dev); // TODO: Better error handling
    deallocateFileDescriptor(fd);
    fileDescriptorsToDeviceHandles.erase(fd);
    deviceNamesToFileDescriptors.erase(deviceName);
  }
}

void XNVMeOp::pread(int fd, void* buf, size_t size, off_t start) {
  auto cmd_ctx = xnvme_file_get_cmd_ctx(getDeviceHandle(fd));
  xnvme_file_pread(&cmd_ctx, buf, size, start);
}

void XNVMeOp::pwrite(int fd, const void* buf, size_t size, off_t start) {
  auto cmd_ctx = xnvme_file_get_cmd_ctx(getDeviceHandle(fd));
  xnvme_file_pwrite(&cmd_ctx, const_cast<void*>(buf), size, start);
}

// Maps an open file descriptor to a device handle
xnvme_dev* XNVMeOp::getDeviceHandle(int fd) {
  // TODO: Implement
  auto iter = fileDescriptorsToDeviceHandles.find(fd);
  return iter !=  fileDescriptorsToDeviceHandles.end() ? iter->second : nullptr; 
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
