
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
    size_t writeBufferSize,
    bool validateWrites = false) {
  auto xnvmeBackend = new XNVMe(numAsyncRequests, device_uri, async_be_opts);
  auto fd = open("/dev/nvme0n1", O_WRONLY);

  size_t completed1 = 0;
  auto fn1 = [&](folly::AsyncBaseOp*) { completed1++; };

  char** writeBuffers = new char*[numAsyncRequests];
  EXPECT_TRUE(writeBuffers != nullptr);
  XNVMeOp** writeOps = new XNVMeOp*[numAsyncRequests];
  EXPECT_TRUE(writeOps != nullptr);
  EXPECT_TRUE(writeBuffers != nullptr);
  EXPECT_TRUE(writeOps != nullptr);

  for (int i = 0; i < numAsyncRequests; i++) {
    writeOps[i] = new XNVMeOp();
    writeOps[i]->setNotificationCallback(fn1);
    EXPECT_TRUE(writeOps[i] != nullptr);
    writeBuffers[i] = new char[writeBufferSize];
    EXPECT_TRUE(writeBuffers[i] != nullptr);
    ::memset(writeBuffers[i], 1, writeBufferSize);
    writeOps[i]->pwrite(fd, writeBuffers[i], writeBufferSize, i);
    xnvmeBackend->submit(writeOps[i]);
  }
  EXPECT_EQ(xnvmeBackend->pending(), numAsyncRequests - completed1);
  xnvmeBackend->wait(numAsyncRequests);
  EXPECT_EQ(xnvmeBackend->pending(), 0);
  EXPECT_EQ(completed1, numAsyncRequests);

  size_t completed2 = 0;
  auto fn2 = [&](folly::AsyncBaseOp*) { completed2++; };
  if (validateWrites) {
    XNVMeOp** readOps = new XNVMeOp*[numAsyncRequests];
    char** readBuffers = new char*[numAsyncRequests];
    for (int i = 0; i < numAsyncRequests; i++) {
      readOps[i] = new XNVMeOp();
      readOps[i]->setNotificationCallback(fn2);
      EXPECT_TRUE(readOps[i] != nullptr);
      readBuffers[i] = new char[writeBufferSize];
      EXPECT_TRUE(writeBuffers[i] != nullptr);
      ::memset(readBuffers[i], 0, writeBufferSize);
      readOps[i]->pread(fd, readBuffers[i], writeBufferSize, i);
      xnvmeBackend->submit(readOps[i]);
    }
    EXPECT_EQ(xnvmeBackend->pending(), numAsyncRequests - completed2);
    xnvmeBackend->wait(numAsyncRequests);
    EXPECT_EQ(xnvmeBackend->pending(), 0);
    EXPECT_EQ(completed2, numAsyncRequests);

    for (int i = 0; i < numAsyncRequests; i++) {
      EXPECT_TRUE(
          memcmp(readBuffers[i], writeBuffers[i], writeBufferSize) == 0);
      EXPECT_EQ(writeBufferSize, readOps[i]->result());
      delete readOps[i];
      delete[] readBuffers[i];
    }
    delete[] readOps;
    delete[] readBuffers;
  }

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

TEST(AsyncCompletionTest, CheckBasicAsyncOperation) {
  auto deviceUri = std::string("/dev/nvme0n1");
  auto ioUringOpts = xnvme_opts_default();
  ioUringOpts.be = "linux";
  ioUringOpts.async = "io_uring";

  XNVMe xnvmeBackend{3, deviceUri, ioUringOpts};
  auto fd = open("/dev/nvme0n1", O_RDWR);

  size_t completed = 0;
  XNVMeOp wOp1, wOp2, wOp3;
  char writeBuf[100];
  ::memset(writeBuf, 1, 100);
  auto fn = [&](folly::AsyncBaseOp*) { completed++; };
  wOp1.setNotificationCallback(fn);
  wOp2.setNotificationCallback(fn);
  wOp3.setNotificationCallback(fn);

  wOp1.pwrite(fd, writeBuf, 100, 0);
  wOp2.pread(fd, writeBuf, 100, 0);
  wOp3.pwrite(fd, writeBuf, 100, 0);

  EXPECT_EQ(0, xnvmeBackend.pending());

  xnvmeBackend.submit(&wOp1);
  xnvmeBackend.submit(&wOp2);
  xnvmeBackend.submit(&wOp3);

  EXPECT_EQ(3, xnvmeBackend.pending());

  xnvmeBackend.wait(3);
  EXPECT_EQ(completed, 3);
  EXPECT_EQ(xnvmeBackend.pending(), 0);
  close(fd);
}

TEST(BlindWriteTest, IouringBackendTest) {
  auto deviceUri = std::string("/dev/nvme0n1");
  auto iouringLinuxBeOpts = xnvme_opts_default();
  iouringLinuxBeOpts.be = "linux";
  iouringLinuxBeOpts.async = "io_uring";
  writeDeviceAsynchronously(iouringLinuxBeOpts, 2048, deviceUri, 1000);
}

TEST(BlindWriteTest, LibAioBackendTest) {
  auto deviceUri = std::string("/dev/nvme0n1");
  auto libAioLinuxBeOpts = xnvme_opts_default();
  libAioLinuxBeOpts.be = "linux";
  libAioLinuxBeOpts.async = "libaio";
  writeDeviceAsynchronously(libAioLinuxBeOpts, 1024, deviceUri, 1000);
}

TEST(WriteReadTest, IouringBackendTest) {
  auto deviceUri = std::string("/dev/nvme0n1");
  auto iouringLinuxBeOpts = xnvme_opts_default();
  iouringLinuxBeOpts.be = "linux";
  iouringLinuxBeOpts.async = "io_uring";
  writeDeviceAsynchronously(iouringLinuxBeOpts, 2048, deviceUri, 1000, true);
}

TEST(WriteReadTest, LibAioBackendTest) {
  auto deviceUri = std::string("/dev/nvme0n1");
  auto libAioLinuxBeOpts = xnvme_opts_default();
  libAioLinuxBeOpts.be = "linux";
  libAioLinuxBeOpts.async = "libaio";
  writeDeviceAsynchronously(libAioLinuxBeOpts, 2, deviceUri, 10, true);
}

TEST(CmdPassThruTest, FlushCommandPassThruTest) {
  auto deviceUri = std::string("/dev/ng0n1");
  auto iouringLinuxBeOpts = xnvme_opts_default();
  iouringLinuxBeOpts.be = "linux";
  iouringLinuxBeOpts.async = "io_uring_cmd";

  XNVMe xnvme {2, deviceUri, iouringLinuxBeOpts};
  XNVMeOp flushCmdOp;
  char dbuf[1024];
  char mbuf[1024];
  flushCmdOp.cmd_pass(dbuf, 1024, mbuf, 1024, [](xnvme_spec_cmd cmd){cmd.common.opcode = 0x11;});
  xnvme.submit(&flushCmdOp);
  xnvme.wait(1);
}
} // namespace folly
