
#include <fcntl.h>
#include <folly/experimental/io/XNVMe.h>
#include <folly/portability/GTest.h>

TEST(SmokeTest, Equality) {
  EXPECT_TRUE(1 == 1);
}

namespace folly {
TEST(BasicTest, InstantiationTest) {
  auto valid_device = std::string("/dev/null");
  auto valid_backend{XNVMe(1, valid_device)};
  EXPECT_EQ(valid_backend.isAvailable(), true);

  auto invalid_device = std::string("/foobar");
  auto invalid_backend{XNVMe(1, invalid_device)};
  EXPECT_EQ(invalid_backend.isAvailable(), false);
}

void writeDeviceAsynchronously(
    xnvme_opts& async_be_opts,
    size_t numAsyncRequests,
    std::string& device_uri,
    size_t writeBufferSize) {
  auto xnvmeBackend = new XNVMe(numAsyncRequests, device_uri, async_be_opts);
  auto fd = open("/dev/nvme0n1", O_WRONLY);

  auto writeBuffer = new char(writeBufferSize);
  ::memset(writeBuffer, 1, writeBufferSize);
  XNVMeOp* writeOps[numAsyncRequests];

  for (int i = 0; i < numAsyncRequests; i++) {
    writeOps[i] = new XNVMeOp();
    writeOps[i]->pwrite(fd, writeBuffer, writeBufferSize, 0);
    xnvmeBackend->submit(writeOps[i]);
  }
  xnvmeBackend->wait(numAsyncRequests);

  // Clean up allocated buiffers
  delete writeBuffer;
  for (int i = 0; i < numAsyncRequests; i++) {
    // Check that all bytes in the buffer have been written
    EXPECT_EQ(writeBufferSize, writeOps[i]->result());
    delete writeOps[i];
  }
  delete xnvmeBackend;
  close(fd);
}

TEST(WriteTest, IouringBackendTest) {
  auto deviceUri = std::string("/dev/nvme0n1");
  auto iouringLinuxBeOpts = xnvme_opts_default();
  iouringLinuxBeOpts.be = "linux";
  iouringLinuxBeOpts.async = "io_uring";
  writeDeviceAsynchronously(iouringLinuxBeOpts, 2048, deviceUri, 100);  
}


TEST(WriteTest, LibAioBackendTest) {
  auto deviceUri = std::string("/dev/nvme0n1");
  auto libAioLinuxBeOpts = xnvme_opts_default();
  libAioLinuxBeOpts.be = "linux";
  libAioLinuxBeOpts.async = "libaio";
  writeDeviceAsynchronously(libAioLinuxBeOpts, 1024, deviceUri, 100);  
}

} // namespace folly