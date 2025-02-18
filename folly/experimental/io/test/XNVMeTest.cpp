
#include <fcntl.h>
#include <folly/experimental/io/XNVMe.h>
#include <folly/portability/GTest.h>

constexpr size_t lbaSize = 512;
constexpr int defaultRepetitions = 1000;
/** Block device used for testing reads and writes, use a device without any data */
auto deviceUri = std::string("/dev/nvme0n1");
/** Char device used for testing io_uring cmd path, use a device without any data */
auto charDeviceUri = std::string("/dev/ng0n1");

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
    bool validateWrites = false,
    int repetitions = defaultRepetitions) {
  while (repetitions-- > 0) {
    auto xnvmeBackend = new XNVMe(numAsyncRequests, device_uri, async_be_opts);
    auto fd = open(device_uri.c_str(), O_RDWR);
    EXPECT_TRUE(writeBufferSize % lbaSize == 0);

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
      ::memset(writeBuffers[i], i % 128, writeBufferSize);
      writeOps[i]->pwrite(
          fd, writeBuffers[i], writeBufferSize, i * writeBufferSize);          
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
        readOps[i]->pread(
            fd, readBuffers[i], writeBufferSize, i * writeBufferSize);
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
}

void asyncOperationBasicTest(xnvme_opts& backendOpts, std::string& deviceUri) {
  auto fd = open(deviceUri.c_str(), O_RDWR);
  XNVMe xnvmeBackend{3, deviceUri, backendOpts};
  size_t completed = 0;
  XNVMeOp wOp1, wOp2, wOp3;
  char writeBuf[lbaSize];
  ::memset(writeBuf, 1, lbaSize);
  auto fn = [&](folly::AsyncBaseOp*) { completed++; };
  wOp1.setNotificationCallback(fn);
  wOp2.setNotificationCallback(fn);
  wOp3.setNotificationCallback(fn);

  wOp1.pwrite(fd, writeBuf, lbaSize, 0);
  wOp2.pread(fd, writeBuf, lbaSize, 0);
  wOp3.pwrite(fd, writeBuf, lbaSize, 0);

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

TEST(IoUringBackend, CheckBasicAsyncOperation) {
  auto ioUringOpts = xnvme_opts_default();
  ioUringOpts.be = "linux";
  ioUringOpts.async = "io_uring";
  asyncOperationBasicTest(ioUringOpts, deviceUri);
}

TEST(LibAioBackend, CheckBasicAsyncOperation) {
  auto libaioOpts = xnvme_opts_default();
  libaioOpts.be = "linux";
  libaioOpts.async = "libaio";
  asyncOperationBasicTest(libaioOpts, deviceUri);
}

TEST(IoUringBackend, BlindWriteTest) {
  auto iouringLinuxBeOpts = xnvme_opts_default();
  iouringLinuxBeOpts.be = "linux";
  iouringLinuxBeOpts.async = "io_uring";
  writeDeviceAsynchronously(iouringLinuxBeOpts, 1, deviceUri, lbaSize);
}

TEST(LibAioBackend, BlindWriteTest) {
  auto libAioLinuxBeOpts = xnvme_opts_default();
  libAioLinuxBeOpts.be = "linux";
  libAioLinuxBeOpts.async = "libaio";
  writeDeviceAsynchronously(libAioLinuxBeOpts, 1, deviceUri, lbaSize);
}

TEST(IoUringBackend, WriteReadTest) {
  auto iouringLinuxBeOpts = xnvme_opts_default();
  iouringLinuxBeOpts.be = "linux";
  iouringLinuxBeOpts.async = "io_uring";
  writeDeviceAsynchronously(
      iouringLinuxBeOpts, 1, deviceUri, 2 * lbaSize, true);
}

TEST(LibAioBackend, WriteReadTest) {
  auto libAioLinuxBeOpts = xnvme_opts_default();
  libAioLinuxBeOpts.be = "linux";
  libAioLinuxBeOpts.async = "libaio";
  writeDeviceAsynchronously(libAioLinuxBeOpts, 2, deviceUri, 2 * lbaSize, true);
}

TEST(IoUringCmdBackend, WriteReadCommandPassThruTest) {
  int repetitions = defaultRepetitions;
  while (repetitions-- > 0) {
    auto iouringLinuxBeOpts = xnvme_opts_default();
    iouringLinuxBeOpts.be = "linux";
    iouringLinuxBeOpts.async = "io_uring_cmd";

    XNVMe xnvme{2, charDeviceUri, iouringLinuxBeOpts};
    XNVMeOp writeCmdOp;

    char* writeBuffer = new char[lbaSize];
    ::memset(writeBuffer, repetitions%128, lbaSize);
    writeCmdOp.cmd_pass(
        writeBuffer, lbaSize, nullptr, 0, [](xnvme_spec_cmd& cmd) {
          cmd.common.opcode = XNVME_SPEC_NVM_OPC_WRITE;
          cmd.nvm.slba = 0;
          cmd.nvm.nlb = 0;
        });
    xnvme.submit(&writeCmdOp);
    xnvme.wait(1);

    XNVMeOp readCmdOp;
    char* readBuffer = new char[lbaSize];
    ::memset(readBuffer, 0, lbaSize);
    readCmdOp.cmd_pass(
        readBuffer, lbaSize, nullptr, 0, [](xnvme_spec_cmd& cmd) {
          cmd.common.opcode = XNVME_SPEC_NVM_OPC_READ;
          cmd.nvm.slba = 0;
          cmd.nvm.nlb = 0;
        });
    xnvme.submit(&readCmdOp);
    xnvme.wait(1);
    EXPECT_EQ(memcmp(writeBuffer, readBuffer, lbaSize), 0);
    delete[] readBuffer;
    delete[] writeBuffer;
  }
}
} // namespace folly
