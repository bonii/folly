
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

  char** writeBuffers = new char*[numAsyncRequests];
  EXPECT_TRUE(writeBuffers != nullptr);  
  XNVMeOp** writeOps = new XNVMeOp*[numAsyncRequests];
  EXPECT_TRUE(writeOps != nullptr);
  EXPECT_TRUE(writeBuffers != nullptr);
  EXPECT_TRUE(writeOps != nullptr);

  for (int i = 0; i < numAsyncRequests; i++) {
    writeOps[i] = new XNVMeOp();
    EXPECT_TRUE(writeOps[i] != nullptr);
    writeBuffers[i] = new char[writeBufferSize];
    EXPECT_TRUE(writeBuffers[i] != nullptr);
    ::memset(writeBuffers[i], 1, writeBufferSize);
    writeOps[i]->pwrite(fd, writeBuffers[i], writeBufferSize, 0);
    xnvmeBackend->submit(writeOps[i]);
  }
  xnvmeBackend->wait(numAsyncRequests);

  for (int i = 0; i < numAsyncRequests; i++) {
    // Check that all bytes in the buffer have been written
    EXPECT_EQ(writeBufferSize, writeOps[i]->result());
    delete writeOps[i];
    delete[] writeBuffers[i];
  }
  delete[] writeOps;
  delete[] writeBuffers;
  delete xnvmeBackend;
  close(fd);
}

TEST(WriteTest, IouringBackendTest) {
  auto deviceUri = std::string("/dev/nvme0n1");
  auto iouringLinuxBeOpts = xnvme_opts_default();
  iouringLinuxBeOpts.be = "linux";
  iouringLinuxBeOpts.async = "io_uring";
  writeDeviceAsynchronously(iouringLinuxBeOpts, 2048, deviceUri, 1000);  
}


TEST(WriteTest, LibAioBackendTest) {
  auto deviceUri = std::string("/dev/nvme0n1");
  auto libAioLinuxBeOpts = xnvme_opts_default();
  libAioLinuxBeOpts.be = "linux";
  libAioLinuxBeOpts.async = "libaio";
  writeDeviceAsynchronously(libAioLinuxBeOpts, 1024, deviceUri, 1000);  
}

} // namespace folly