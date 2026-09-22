/*
 * Copyright (C) 2017 CAMELab
 *
 * This file is part of SimpleSSD.
 *
 * SimpleSSD is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SimpleSSD is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with SimpleSSD.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "sil/nvme/nvme.hh"

#include <cstring>
#include <limits>
#include <utility>

#include "simplessd/csd/config.hh"
#include "simplessd/hil/nvme/controller.hh"
#include "simplessd/hil/nvme/def.hh"
#include "simplessd/sim/cpu.hh"
#include "simplessd/util/algorithm.hh"

namespace SIL {

namespace NVMe {

namespace {

const uint64_t CSD_DESCRIPTOR_BYTES = 56;
const uint64_t CSD_VECTOR_OFFSET = CSD_DESCRIPTOR_BYTES;

bool checkedAdd(uint64_t a, uint64_t b, uint64_t &out) {
  if (a > std::numeric_limits<uint64_t>::max() - b) {
    return false;
  }

  out = a + b;

  return true;
}

bool checkedMul(uint64_t a, uint64_t b, uint64_t &out) {
  if (a != 0 && b > std::numeric_limits<uint64_t>::max() / a) {
    return false;
  }

  out = a * b;

  return true;
}

uint64_t alignUp(uint64_t value, uint64_t align) {
  uint64_t add;

  if (align == 0 || !checkedAdd(value, align - 1, add)) {
    SimpleSSD::panic("CSD read_compute BIO has invalid descriptor layout");
  }

  return add / align * align;
}

void store16(std::vector<uint8_t> &buffer, uint64_t offset, uint16_t value) {
  buffer[offset + 0] = value & 0xFF;
  buffer[offset + 1] = (value >> 8) & 0xFF;
}

void store32(std::vector<uint8_t> &buffer, uint64_t offset, uint32_t value) {
  buffer[offset + 0] = value & 0xFF;
  buffer[offset + 1] = (value >> 8) & 0xFF;
  buffer[offset + 2] = (value >> 16) & 0xFF;
  buffer[offset + 3] = (value >> 24) & 0xFF;
}

void store64(std::vector<uint8_t> &buffer, uint64_t offset, uint64_t value) {
  store32(buffer, offset, value & 0xFFFFFFFF);
  store32(buffer, offset + 4, value >> 32);
}

struct SGLBuffer {
  std::vector<uint8_t> storage;

  explicit SGLBuffer(uint64_t size) : storage(size, 0) {}

  void getDescriptor(uint64_t &data1, uint64_t &data2) {
    data1 = (uint64_t)storage.data();
    data2 = (uint64_t)(uint32_t)storage.size();
  }

  void readData(uint64_t offset, uint64_t size, uint8_t *buffer) {
    memcpy(buffer, storage.data() + offset, size);
  }

  void writeData(uint64_t offset, uint64_t size, uint8_t *buffer) {
    memcpy(storage.data() + offset, buffer, size);
  }
};

struct BufferCommandContext {
  PRP *prp;
  SGLBuffer *sgl;
  std::vector<uint8_t> *buffer;
  bool readBack;
  std::function<void(uint16_t)> callback;

  BufferCommandContext(PRP *p, std::vector<uint8_t> *b, bool r,
                       std::function<void(uint16_t)> f)
      : prp(p), sgl(nullptr), buffer(b), readBack(r), callback(f) {}

  BufferCommandContext(SGLBuffer *s, std::vector<uint8_t> *b, bool r,
                       std::function<void(uint16_t)> f)
      : prp(nullptr), sgl(s), buffer(b), readBack(r), callback(f) {}

  ~BufferCommandContext() {
    delete prp;
    delete sgl;
  }

  void getPointer(uint64_t &data1, uint64_t &data2) {
    if (prp) {
      prp->getPointer(data1, data2);
    }
    else {
      sgl->getDescriptor(data1, data2);
    }
  }

  void readData(uint64_t offset, uint64_t size, uint8_t *data) {
    if (prp) {
      prp->readData(offset, size, data);
    }
    else {
      sgl->readData(offset, size, data);
    }
  }

  void writeData(uint64_t offset, uint64_t size, uint8_t *data) {
    if (prp) {
      prp->writeData(offset, size, data);
    }
    else {
      sgl->writeData(offset, size, data);
    }
  }
};

struct CallbackContext {
  std::function<void(uint16_t)> callback;

  explicit CallbackContext(std::function<void(uint16_t)> f) : callback(f) {}
};

struct CSDIOContext {
  PRP *prp;
  SGLBuffer *sgl;
  std::shared_ptr<BIL::CSDGEMVRequest> request;
  std::vector<uint8_t> control;
  uint64_t outputOffset;
  uint64_t outputBytes;
  uint64_t id;
  std::function<void(uint64_t, uint16_t)> callback;

  CSDIOContext(PRP *p, std::shared_ptr<BIL::CSDGEMVRequest> r,
               std::vector<uint8_t> &&c, uint64_t outOffset,
               uint64_t outBytes, uint64_t bioID,
               std::function<void(uint64_t, uint16_t)> f)
      : prp(p),
        sgl(nullptr),
        request(r),
        control(std::move(c)),
        outputOffset(outOffset),
        outputBytes(outBytes),
        id(bioID),
        callback(f) {}

  CSDIOContext(SGLBuffer *s, std::shared_ptr<BIL::CSDGEMVRequest> r,
               std::vector<uint8_t> &&c, uint64_t outOffset,
               uint64_t outBytes, uint64_t bioID,
               std::function<void(uint64_t, uint16_t)> f)
      : prp(nullptr),
        sgl(s),
        request(r),
        control(std::move(c)),
        outputOffset(outOffset),
        outputBytes(outBytes),
        id(bioID),
        callback(f) {}

  ~CSDIOContext() {
    delete prp;
    delete sgl;
  }

  void getPointer(uint64_t &data1, uint64_t &data2) {
    if (prp) {
      prp->getPointer(data1, data2);
    }
    else {
      sgl->getDescriptor(data1, data2);
    }
  }

  void readData(uint64_t offset, uint64_t size, uint8_t *data) {
    if (prp) {
      prp->readData(offset, size, data);
    }
    else {
      sgl->readData(offset, size, data);
    }
  }

  void writeData(uint64_t offset, uint64_t size, uint8_t *data) {
    if (prp) {
      prp->writeData(offset, size, data);
    }
    else {
      sgl->writeData(offset, size, data);
    }
  }
};

}  // namespace

Driver::Driver(Engine &e, SimpleSSD::ConfigReader &conf)
    : BIL::DriverInterface(e),
      dmaReadPending(false),
      dmaWritePending(false),
      csdMaxControlBytes(
          conf.readUint(SimpleSSD::CONFIG_CSD,
                        SimpleSSD::CSD::CSD_MAX_CONTROL_BYTES)),
      phase(true),
      adminSQ(nullptr),
      adminCQ(nullptr),
      ioSQ(nullptr),
      ioCQ(nullptr) {
  pcieGen = (SimpleSSD::PCIExpress::PCIE_GEN)conf.readInt(
      SimpleSSD::CONFIG_NVME, SimpleSSD::HIL::NVMe::NVME_PCIE_GEN);
  pcieLane = (uint8_t)conf.readUint(SimpleSSD::CONFIG_NVME,
                                    SimpleSSD::HIL::NVMe::NVME_PCIE_LANE);

  pController = new SimpleSSD::HIL::NVMe::Controller(this, conf);

  dmaReadEvent = engine.allocateEvent([this](uint64_t) { dmaReadDone(); });
  dmaWriteEvent = engine.allocateEvent([this](uint64_t) { dmaWriteDone(); });
}

Driver::~Driver() {
  delete pController;
  delete adminSQ;
  delete adminCQ;
  delete ioSQ;
  delete ioCQ;
}

void Driver::init(std::function<void()> &func) {
  beginFunction = func;

  // NVMe Initialization process (Register)
  // See Section 7.6.1. Initialization of NVMe 1.3c
  union {
    uint64_t value;
    uint8_t buffer[8];
  } temp;
  uint64_t tick = 0;

  // Step 1. Read CAP
  pController->readRegister(SimpleSSD::HIL::NVMe::REG_CONTROLLER_CAPABILITY, 8,
                            temp.buffer, tick);

  // MPSMAX/MIN is setted to 4KB
  // DSTRD is setted to 0 (4bytes)
  // Check MQES
  maxQueueEntries = (temp.value & 0xFFFF) + 1;

  // Step 2. Wait for CSTS.RDY = 0
  // Step 3. Configure admin queue
  // Step 3-1. Set admin queue entry size
  uint16_t entries = QUEUE_ENTRY_ADMIN;

  if (entries > maxQueueEntries) {
    entries = maxQueueEntries;
  }

  temp.value = entries - 1;
  temp.value |= (entries - 1) << 16;

  pController->writeRegister(SimpleSSD::HIL::NVMe::REG_ADMIN_QUEUE_ATTRIBUTE, 4,
                             temp.buffer, tick);

  adminSQ = new Queue(entries, 64);
  adminCQ = new Queue(entries, 16);

  // Step 3-2. Write base addresses
  adminSQ->getBaseAddress(temp.value);
  pController->writeRegister(SimpleSSD::HIL::NVMe::REG_ADMIN_SQUEUE_BASE_ADDR,
                             8, temp.buffer, tick);
  adminCQ->getBaseAddress(temp.value);
  pController->writeRegister(SimpleSSD::HIL::NVMe::REG_ADMIN_CQUEUE_BASE_ADDR,
                             8, temp.buffer, tick);

  // Step 4. Configure controller
  // Step 5. Enable controller
  temp.value = 1;            // Round Robin, 4K page, NVM command set, Enable
  temp.value |= 0x00460000;  // 64B SQEntry, 16B CQEntry
  pController->writeRegister(SimpleSSD::HIL::NVMe::REG_CONTROLLER_CONFIG, 4,
                             temp.buffer, tick);

  // Step 6. Wait for CSTS.RDY = 1
  // Step 7. Send Identify
  // Step 7-1. Submit Identify Controller
  uint32_t cmd[16];
  PRP *prp = new PRP(4096);
  ResponseHandler callback = [this](uint16_t status, uint32_t, void *context) {
    _init0(status, context);
  };

  memset(cmd, 0, 64);
  cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_IDENTIFY;  // CID, FUSE, OPC
  prp->getPointer(*(uint64_t *)(cmd + 6), *(uint64_t *)(cmd + 8));  // DPTR
  cmd[10] = SimpleSSD::HIL::NVMe::CNS_IDENTIFY_CONTROLLER;          // CNS

  submitCommand(0, (uint8_t *)cmd, callback, prp);
}

void Driver::_init0(uint16_t, void *context) {
  PRP *prp = (PRP *)context;

  // Step 7-2. Send Identify Active Namespace List
  // Reuse PRP here
  uint32_t cmd[16];
  ResponseHandler callback = [this](uint16_t status, uint32_t, void *context) {
    _init1(status, context);
  };

  memset(cmd, 0, 64);
  cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_IDENTIFY;  // CID, FUSE, OPC
  prp->getPointer(*(uint64_t *)(cmd + 6), *(uint64_t *)(cmd + 8));  // DPTR
  cmd[10] = SimpleSSD::HIL::NVMe::CNS_ACTIVE_NAMESPACE_LIST;        // CNS

  submitCommand(0, (uint8_t *)cmd, callback, prp);
}

void Driver::_init1(uint16_t, void *context) {
  PRP *prp = (PRP *)context;

  // Step 7-3. Check active Namespace
  // We will perform I/O on first Namespace
  uint32_t count = 0;
  uint32_t nsid = 0;

  for (count = 0; count < 1024; count++) {
    prp->readData(count * 4, 4, (uint8_t *)&nsid);

    if (nsid == 0) {
      break;
    }

    if (count == 0) {
      namespaceID = nsid;
    }
  }

  if (count == 0) {
    SimpleSSD::panic("This NVMe SSD does not have any namespaces.");
  }
  else if (count > 1) {
    SimpleSSD::warn("This NVMe SSD has %u namespaces.", count);
    SimpleSSD::warn("All I/O will performed on namespace ID %u.", namespaceID);
  }

  // Step 7-4. Send Identify Namespace
  // Reuse PRP here
  uint32_t cmd[16];
  ResponseHandler callback = [this](uint16_t status, uint32_t, void *context) {
    _init2(status, context);
  };

  memset(cmd, 0, 64);
  cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_IDENTIFY;  // CID, FUSE, OPC
  cmd[1] = namespaceID;                            // NSID
  prp->getPointer(*(uint64_t *)(cmd + 6), *(uint64_t *)(cmd + 8));  // DPTR
  cmd[10] = SimpleSSD::HIL::NVMe::CNS_IDENTIFY_NAMESPACE;           // CNS

  submitCommand(0, (uint8_t *)cmd, callback, prp);
}

void Driver::_init2(uint16_t, void *context) {
  union {
    uint64_t value;
    uint8_t buffer[8];
  } temp;
  PRP *prp = (PRP *)context;
  uint8_t nFormat, currentFormat;
  uint32_t formatData;

  // Step 7-4. Check structures
  prp->readData(0, 8, temp.buffer);
  capacity = temp.value;

  prp->readData(25, 1, &nFormat);
  nFormat++;

  prp->readData(26, 1, &currentFormat);
  prp->readData(128 + currentFormat * 4ull, 4, (uint8_t *)&formatData);

  LBAsize = (uint32_t)powf(2.f, (float)((formatData >> 16) & 0xFF));
  capacity *= LBAsize;

  delete prp;

  SimpleSSD::info("SIL::NVMe::Driver: Total SSD capacity: %" PRIu64 " bytes",
                  capacity);
  SimpleSSD::info("SIL::NVMe::Driver: Logical Block Size: %" PRIu32 " bytes",
                  LBAsize);

  // Step 8. Determine I/O queue count
  // Step 8-1. Send Set Feature
  uint32_t cmd[16];
  ResponseHandler callback = [this](uint16_t status, uint32_t dw0,
                                    void *context) {
    _init3(status, dw0, context);
  };

  memset(cmd, 0, 64);
  cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_SET_FEATURES;        // CID, FUSE, OPC
  cmd[10] = SimpleSSD::HIL::NVMe::FEATURE_NUMBER_OF_QUEUES;  // FID
  cmd[11] = 0x00000000;  // One I/O SQ, One I/O CQ

  submitCommand(0, (uint8_t *)cmd, callback, nullptr);
}

void Driver::_init3(uint16_t, uint32_t dw0, void *) {
  // Step 8-2. Check response
  if (dw0 != 0x00000000) {
    SimpleSSD::warn("NVMe SSD responsed too many I/O queue");
  }

  // Step 9. Allocate I/O Completion Queue
  // Step 9-1. Send Create I/O Completion Queue
  uint32_t cmd[16];
  uint16_t entries = QUEUE_ENTRY_IO;
  ResponseHandler callback = [this](uint16_t status, uint32_t, void *context) {
    _init4(status, context);
  };

  if (entries > maxQueueEntries) {
    entries = maxQueueEntries;
  }

  ioCQ = new Queue(entries, 16);

  memset(cmd, 0, 64);
  cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_CREATE_IO_CQUEUE;  // CID, FUSE, OPC
  ioCQ->getBaseAddress(*(uint64_t *)(cmd + 6));            // DPTR.PRP1
  cmd[10] = ((uint32_t)(entries - 1) << 16) | 0x0001;      // QSIZE, QID
  cmd[11] = 0x00010003;                                    // IV, IEN, PC

  submitCommand(0, (uint8_t *)cmd, callback, nullptr);
}

void Driver::_init4(uint16_t status, void *) {
  // Step 9-2. Check result
  if (status != 0) {
    SimpleSSD::panic("Failed to create I/O Completion Queue");
  }

  // Step 10. Allocate I/O Submission Queue
  // Step 10-1. Send Create I/O Submission Queue
  uint32_t cmd[16];
  uint16_t entries = QUEUE_ENTRY_IO;
  ResponseHandler callback = [this](uint16_t status, uint32_t, void *context) {
    _init5(status, context);
  };

  if (entries > maxQueueEntries) {
    entries = maxQueueEntries;
  }

  ioSQ = new Queue(entries, 64);

  memset(cmd, 0, 64);
  cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_CREATE_IO_SQUEUE;  // CID, FUSE, OPC
  ioSQ->getBaseAddress(*(uint64_t *)(cmd + 6));            // DPTR.PRP1
  cmd[10] = ((uint32_t)(entries - 1) << 16) | 0x0001;      // QSIZE, QID
  cmd[11] = 0x00010001;                                    // CQID, QPRIO, PC

  submitCommand(0, (uint8_t *)cmd, callback, nullptr);
}

void Driver::_init5(uint16_t status, void *) {
  // Step 10-2. Check result
  if (status != 0) {
    SimpleSSD::panic("Failed to create I/O Submission Queue");
  }

  SimpleSSD::info("SIL::NVMe::Driver: Initialization finished");

  // Now we initialized NVMe SSD
  beginFunction();
}

void Driver::submitCommand(uint16_t iv, uint8_t *cmd, ResponseHandler &func,
                           void *context) {
  uint16_t cid = 0;
  uint16_t opcode = cmd[0];
  uint16_t tail = 0;
  uint64_t tick = engine.getCurrentTick();
  Queue *queue = nullptr;

  // Push to queue
  if (iv == 0) {
    increaseCommandID(adminCommandID);
    cid = adminCommandID;
    queue = adminSQ;
  }
  else if (iv == 1 && ioSQ) {
    increaseCommandID(ioCommandID);
    cid = ioCommandID;
    queue = ioSQ;
  }
  else {
    SimpleSSD::panic("I/O Submission Queue is not initialized");
  }

  memcpy(cmd + 2, &cid, 2);
  queue->setData(cmd, 64);
  tail = queue->getTail();

  // Push to pending cmd list
  pendingCommandList.push_back(CommandEntry(iv, opcode, cid, context, func));

  // Ring doorbell
  pController->ringSQTailDoorbell(iv, tail, tick);
  queue->incrHead();
}

void Driver::increaseCommandID(uint16_t &id) {
  static const uint16_t maxID = 32767;

  id++;

  if (id > maxID) {
    id = 1;
  }
}

void Driver::getInfo(uint64_t &bytesize, uint32_t &minbs) {
  bytesize = capacity;
  minbs = LBAsize;
}

void Driver::submitIO(BIL::BIO &bio) {
  uint32_t cmd[16];
  PRP *prp = nullptr;
  static ResponseHandler callback = [this](uint16_t status, uint32_t,
                                           void *context) {
    _io(status, context);
  };

  memset(cmd, 0, 64);

  uint64_t slba = bio.offset / LBAsize;
  uint32_t nlb = (uint32_t)DIVCEIL(bio.length, LBAsize);
  uint64_t transferBytes = (uint64_t)nlb * LBAsize;

  cmd[1] = namespaceID;  // NSID

  if (bio.type == BIL::BIO_READ) {
    cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_READ;  // CID, FUSE, OPC
    cmd[10] = (uint32_t)slba;
    cmd[11] = slba >> 32;
    cmd[12] = nlb - 1;  // LR, FUA, PRINFO, NLB

    prp = new PRP(transferBytes);
    prp->getPointer(*(uint64_t *)(cmd + 6), *(uint64_t *)(cmd + 8));  // DPTR
  }
  else if (bio.type == BIL::BIO_WRITE) {
    cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_WRITE;  // CID, FUSE, OPC
    cmd[10] = (uint32_t)slba;
    cmd[11] = slba >> 32;
    cmd[12] = nlb - 1;  // LR, FUA, PRINFO, DTYPE, NLB

    prp = new PRP(transferBytes);

    if (bio.payload) {
      if (bio.payload->size() < bio.length) {
        SimpleSSD::panic("NVMe BIO write payload is shorter than length");
      }

      prp->writeData(0, bio.length, bio.payload->data());
    }

    prp->getPointer(*(uint64_t *)(cmd + 6), *(uint64_t *)(cmd + 8));  // DPTR
  }
  else if (bio.type == BIL::BIO_FLUSH) {
    cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_FLUSH;  // CID, FUSE, OPC
  }
  else if (bio.type == BIL::BIO_TRIM) {
    cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_DATASET_MANAGEMEMT;  // CID, FUSE, OPC
    cmd[10] = 0;                                               // NR
    cmd[11] = 0x04;                                            // AD

    prp = new PRP(16);
    prp->getPointer(*(uint64_t *)(cmd + 6), *(uint64_t *)(cmd + 8));  // DPTR

    // Fill range definition
    uint8_t data[16];

    memset(data, 0, 16);
    memcpy(data + 4, &nlb, 4);
    memcpy(data + 8, &slba, 8);

    prp->writeData(0, 16, data);
  }
  else if (bio.type == BIL::BIO_READ_COMPUTE) {
    if (!bio.csd) {
      SimpleSSD::panic("CSD read_compute BIO has no GEMV descriptor");
    }
    if (bio.csd->rows == 0 || bio.csd->cols == 0) {
      SimpleSSD::panic("CSD read_compute BIO has invalid dimensions");
    }
    if (bio.csd->vectorFP16.size() != bio.csd->cols) {
      SimpleSSD::panic("CSD read_compute BIO vector length mismatch");
    }

    uint32_t lda = bio.csd->lda == 0 ? bio.csd->cols : bio.csd->lda;
    uint64_t vectorBytes;
    uint64_t outputBytes;
    uint64_t vectorEnd;
    uint64_t outputOffset;
    uint64_t minimumControlBytes;

    if (!checkedMul(bio.csd->cols, sizeof(uint16_t), vectorBytes) ||
        !checkedMul(bio.csd->rows, sizeof(float), outputBytes) ||
        !checkedAdd(CSD_VECTOR_OFFSET, vectorBytes, vectorEnd)) {
      SimpleSSD::panic("CSD read_compute BIO has invalid descriptor layout");
    }

    outputOffset = alignUp(vectorEnd, sizeof(float));

    if (!checkedAdd(outputOffset, outputBytes, minimumControlBytes)) {
      SimpleSSD::panic("CSD read_compute BIO has invalid descriptor layout");
    }
    uint64_t controlBytes = bio.csd->controlBytes == 0
                                ? minimumControlBytes
                                : bio.csd->controlBytes;

    if (lda != bio.csd->cols || controlBytes < minimumControlBytes ||
        controlBytes > csdMaxControlBytes ||
        controlBytes > std::numeric_limits<uint32_t>::max()) {
      SimpleSSD::panic("CSD read_compute BIO has invalid descriptor layout");
    }

    std::vector<uint8_t> control(controlBytes, 0);

    control[0] = 'C';
    control[1] = 'S';
    control[2] = 'D';
    control[3] = '0';
    store16(control, 4, 1);
    store16(control, 6, 1);
    store32(control, 8, 0);
    store64(control, 16, bio.csd->matrixSLBA);
    store32(control, 24, bio.csd->rows);
    store32(control, 28, bio.csd->cols);
    store32(control, 32, lda);
    store64(control, 40, CSD_VECTOR_OFFSET);
    store64(control, 48, outputOffset);

    for (uint32_t i = 0; i < bio.csd->cols; i++) {
      store16(control, CSD_VECTOR_OFFSET + (uint64_t)i * sizeof(uint16_t),
              bio.csd->vectorFP16[i]);
    }

    bio.csd->outputFP32.assign(bio.csd->rows, 0.f);

    CSDIOContext *context;
    ResponseHandler csdCallback = [](uint16_t status, uint32_t,
                                     void *opaque) {
      auto context = (CSDIOContext *)opaque;

      if (status == 0) {
        std::vector<uint8_t> raw(context->outputBytes, 0);

        context->readData(context->outputOffset, context->outputBytes,
                          raw.data());

        for (uint32_t i = 0; i < context->request->rows; i++) {
          memcpy(context->request->outputFP32.data() + i,
                 raw.data() + (uint64_t)i * sizeof(float), sizeof(float));
        }
      }
      else {
        SimpleSSD::warn("CSD read_compute BIO error: %04X", status);
      }

      context->callback(context->id, status);

      delete context;
    };

    if (bio.csd->useSGL) {
      context = new CSDIOContext(new SGLBuffer(controlBytes), bio.csd,
                                 std::move(control), outputOffset, outputBytes,
                                 bio.id, bio.callback);
    }
    else {
      context = new CSDIOContext(new PRP(controlBytes), bio.csd,
                                 std::move(control), outputOffset, outputBytes,
                                 bio.id, bio.callback);
    }

    context->writeData(0, controlBytes, context->control.data());
    context->getPointer(*(uint64_t *)(cmd + 6), *(uint64_t *)(cmd + 8));

    cmd[0] = bio.csd->opcode;
    cmd[10] = (uint32_t)controlBytes;

    if (bio.csd->useSGL) {
      ((uint8_t *)cmd)[1] = 0x40;
    }

    submitCommand(1, (uint8_t *)cmd, csdCallback, context);

    return;
  }
  else {
    SimpleSSD::panic("Unsupported BIO type");
  }

  submitCommand(1, (uint8_t *)cmd, callback,
                new IOWrapper(bio.id, prp, bio.callback));
}

void Driver::submitWriteBuffer(uint64_t offset, const uint8_t *buffer,
                               uint64_t length,
                               std::function<void(uint16_t)> callback) {
  uint32_t cmd[16];
  uint64_t slba = offset / LBAsize;
  uint32_t nlb = (uint32_t)DIVCEIL(length, LBAsize);
  uint64_t transferBytes = (uint64_t)nlb * LBAsize;
  PRP *prp = new PRP(transferBytes);
  auto context = new BufferCommandContext(prp, nullptr, false, callback);
  ResponseHandler done = [](uint16_t status, uint32_t, void *opaque) {
    auto context = (BufferCommandContext *)opaque;

    context->callback(status);

    delete context;
  };

  memset(cmd, 0, 64);

  if (length == 0 || offset % LBAsize != 0) {
    SimpleSSD::panic("CSD smoke write should be LBA-aligned and non-empty");
  }

  prp->writeData(0, length, const_cast<uint8_t *>(buffer));
  prp->getPointer(*(uint64_t *)(cmd + 6), *(uint64_t *)(cmd + 8));

  cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_WRITE;
  cmd[1] = namespaceID;
  cmd[10] = (uint32_t)slba;
  cmd[11] = slba >> 32;
  cmd[12] = nlb - 1;

  submitCommand(1, (uint8_t *)cmd, done, context);
}

void Driver::submitReadBuffer(uint64_t offset, std::vector<uint8_t> &buffer,
                              std::function<void(uint16_t)> callback) {
  uint32_t cmd[16];
  uint64_t slba = offset / LBAsize;
  uint32_t nlb = (uint32_t)DIVCEIL(buffer.size(), LBAsize);
  uint64_t transferBytes = (uint64_t)nlb * LBAsize;
  PRP *prp = new PRP(transferBytes);
  auto context = new BufferCommandContext(prp, &buffer, true, callback);
  ResponseHandler done = [](uint16_t status, uint32_t, void *opaque) {
    auto context = (BufferCommandContext *)opaque;

    if (status == 0 && context->buffer) {
      context->readData(0, context->buffer->size(), context->buffer->data());
    }

    context->callback(status);

    delete context;
  };

  memset(cmd, 0, 64);

  if (buffer.size() == 0 || offset % LBAsize != 0 ||
      buffer.size() % LBAsize != 0) {
    SimpleSSD::panic("NVMe read buffer should be LBA-aligned and non-empty");
  }

  context->getPointer(*(uint64_t *)(cmd + 6), *(uint64_t *)(cmd + 8));

  cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_READ;
  cmd[1] = namespaceID;
  cmd[10] = (uint32_t)slba;
  cmd[11] = slba >> 32;
  cmd[12] = nlb - 1;

  submitCommand(1, (uint8_t *)cmd, done, context);
}

void Driver::submitCompareBuffer(uint64_t offset, const uint8_t *buffer,
                                 uint64_t length,
                                 std::function<void(uint16_t)> callback) {
  uint32_t cmd[16];
  uint64_t slba = offset / LBAsize;
  uint32_t nlb = (uint32_t)DIVCEIL(length, LBAsize);
  uint64_t transferBytes = (uint64_t)nlb * LBAsize;
  PRP *prp = new PRP(transferBytes);
  auto context = new BufferCommandContext(prp, nullptr, false, callback);
  ResponseHandler done = [](uint16_t status, uint32_t, void *opaque) {
    auto context = (BufferCommandContext *)opaque;

    context->callback(status);

    delete context;
  };

  memset(cmd, 0, 64);

  if (length == 0 || offset % LBAsize != 0 || length % LBAsize != 0) {
    SimpleSSD::panic("NVMe compare buffer should be LBA-aligned and non-empty");
  }

  context->writeData(0, length, const_cast<uint8_t *>(buffer));
  context->getPointer(*(uint64_t *)(cmd + 6), *(uint64_t *)(cmd + 8));

  cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_COMPARE;
  cmd[1] = namespaceID;
  cmd[10] = (uint32_t)slba;
  cmd[11] = slba >> 32;
  cmd[12] = nlb - 1;

  submitCommand(1, (uint8_t *)cmd, done, context);
}

void Driver::submitTrim(uint64_t offset, uint64_t length,
                        std::function<void(uint16_t)> callback) {
  uint32_t cmd[16];
  uint64_t slba = offset / LBAsize;
  uint32_t nlb = (uint32_t)DIVCEIL(length, LBAsize);
  PRP *prp = new PRP(16);
  auto context = new BufferCommandContext(prp, nullptr, false, callback);
  ResponseHandler done = [](uint16_t status, uint32_t, void *opaque) {
    auto context = (BufferCommandContext *)opaque;

    context->callback(status);

    delete context;
  };
  uint8_t range[16];

  memset(cmd, 0, 64);
  memset(range, 0, sizeof(range));

  if (length == 0 || offset % LBAsize != 0 || length % LBAsize != 0) {
    SimpleSSD::panic("NVMe trim range should be LBA-aligned and non-empty");
  }

  memcpy(range + 4, &nlb, sizeof(nlb));
  memcpy(range + 8, &slba, sizeof(slba));
  context->writeData(0, sizeof(range), range);
  context->getPointer(*(uint64_t *)(cmd + 6), *(uint64_t *)(cmd + 8));

  cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_DATASET_MANAGEMEMT;
  cmd[1] = namespaceID;
  cmd[10] = 0;
  cmd[11] = 0x04;

  submitCommand(1, (uint8_t *)cmd, done, context);
}

void Driver::submitFormat(bool secureErase,
                          std::function<void(uint16_t)> callback) {
  uint32_t cmd[16];
  auto context = new CallbackContext(callback);
  ResponseHandler done = [](uint16_t status, uint32_t, void *opaque) {
    auto context = (CallbackContext *)opaque;

    context->callback(status);

    delete context;
  };

  memset(cmd, 0, sizeof(cmd));

  cmd[0] = SimpleSSD::HIL::NVMe::OPCODE_FORMAT_NVM;
  cmd[1] = namespaceID;
  cmd[10] = secureErase ? 0x0200 : 0x0000;

  submitCommand(0, (uint8_t *)cmd, done, context);
}

void Driver::submitCSDReadCompute(std::vector<uint8_t> &control,
                                  std::function<void(uint16_t)> callback) {
  submitCSDReadCompute(control, SimpleSSD::HIL::NVMe::OPCODE_CSD_READ_COMPUTE,
                       false, callback);
}

void Driver::submitCSDReadCompute(std::vector<uint8_t> &control,
                                  uint8_t opcode, bool useSGL,
                                  std::function<void(uint16_t)> callback) {
  submitCSDReadComputeForNamespace(control, namespaceID, opcode, useSGL,
                                   callback);
}

void Driver::submitCSDReadComputeForNamespace(
    std::vector<uint8_t> &control, uint32_t nsid, uint8_t opcode, bool useSGL,
    std::function<void(uint16_t)> callback) {
  if (control.size() > std::numeric_limits<uint32_t>::max()) {
    SimpleSSD::panic("CSD read_compute control buffer is too large");
  }

  submitCSDReadComputeForNamespaceWithCommandBytes(
      control, nsid, (uint32_t)control.size(), opcode, useSGL, callback);
}

void Driver::submitCSDReadComputeForNamespaceWithCommandBytes(
    std::vector<uint8_t> &control, uint32_t nsid, uint32_t commandControlBytes,
    uint8_t opcode, bool useSGL, std::function<void(uint16_t)> callback) {
  uint32_t cmd[16];
  BufferCommandContext *context;
  ResponseHandler done = [](uint16_t status, uint32_t, void *opaque) {
    auto context = (BufferCommandContext *)opaque;

    if (status == 0 && context->readBack && context->buffer) {
      context->readData(0, context->buffer->size(), context->buffer->data());
    }

    context->callback(status);

    delete context;
  };

  memset(cmd, 0, 64);

  if (control.size() == 0) {
    SimpleSSD::panic("CSD read_compute control buffer should be non-empty");
  }

  if (useSGL) {
    context = new BufferCommandContext(new SGLBuffer(control.size()), &control,
                                       true, callback);
  }
  else {
    context = new BufferCommandContext(new PRP(control.size()), &control, true,
                                       callback);
  }

  context->writeData(0, control.size(), control.data());
  context->getPointer(*(uint64_t *)(cmd + 6), *(uint64_t *)(cmd + 8));

  cmd[0] = opcode;
  cmd[1] = nsid;
  cmd[10] = commandControlBytes;
  if (useSGL) {
    ((uint8_t *)cmd)[1] = 0x40;
  }

  submitCommand(1, (uint8_t *)cmd, done, context);
}

void Driver::_io(uint16_t status, void *context) {
  IOWrapper *wrapper = (IOWrapper *)context;
  PRP *prp = wrapper->prp;

  if (status != 0) {
    SimpleSSD::warn("I/O error: %04X", status);
  }

  wrapper->bioCallback(wrapper->id, status);

  delete prp;
  delete wrapper;
}

void Driver::initStats(std::vector<SimpleSSD::Stats> &list) {
  pController->getStatList(list, "");
  SimpleSSD::getCPUStatList(list, "cpu");
}

void Driver::getStats(std::vector<double> &values) {
  pController->getStatValues(values);
  SimpleSSD::getCPUStatValues(values);
}

void Driver::resetStats() {
  pController->resetStatValues();
  SimpleSSD::resetCPUStatValues();
}

void Driver::dmaRead(uint64_t addr, uint64_t size, uint8_t *buffer,
                     SimpleSSD::DMAFunction &func, void *context) {
  if (size == 0) {
    SimpleSSD::warn("nvme_interface: zero-size DMA read request. Ignore.");

    return;
  }

  dmaReadQueue.push(DMAEntry(func));

  auto &iter = dmaReadQueue.back();
  iter.addr = addr;
  iter.size = size;
  iter.buffer = buffer;
  iter.context = context;

  if (!dmaReadPending) {
    submitDMARead();
  }
}

void Driver::dmaReadDone() {
  auto &iter = dmaReadQueue.front();
  uint64_t tick = engine.getCurrentTick();

  if (tick < iter.finishedAt) {
    engine.scheduleEvent(dmaReadEvent, iter.finishedAt);

    return;
  }

  iter.func(tick, iter.context);
  dmaReadQueue.pop();
  dmaReadPending = false;

  if (dmaReadQueue.size() > 0) {
    submitDMARead();
  }
}

void Driver::submitDMARead() {
  auto &iter = dmaReadQueue.front();

  dmaReadPending = true;

  iter.beginAt = engine.getCurrentTick();
  iter.finishedAt = iter.beginAt + SimpleSSD::PCIExpress::calculateDelay(
                                       pcieGen, pcieLane, iter.size);

  if (iter.buffer) {
    memcpy(iter.buffer, (uint8_t *)iter.addr, iter.size);
  }

  engine.scheduleEvent(dmaReadEvent, iter.finishedAt);
}

void Driver::dmaWrite(uint64_t addr, uint64_t size, uint8_t *buffer,
                      SimpleSSD::DMAFunction &func, void *context) {
  if (size == 0) {
    SimpleSSD::warn("nvme_interface: zero-size DMA write request. Ignore.");

    return;
  }

  dmaWriteQueue.push(DMAEntry(func));

  auto &iter = dmaWriteQueue.back();
  iter.addr = addr;
  iter.size = size;
  iter.buffer = buffer;
  iter.context = context;

  if (!dmaWritePending) {
    submitDMAWrite();
  }
}

void Driver::dmaWriteDone() {
  auto &iter = dmaWriteQueue.front();
  uint64_t tick = engine.getCurrentTick();

  if (tick < iter.finishedAt) {
    engine.scheduleEvent(dmaWriteEvent, iter.finishedAt);

    return;
  }

  iter.func(tick, iter.context);
  dmaWriteQueue.pop();
  dmaWritePending = false;

  if (dmaWriteQueue.size() > 0) {
    submitDMAWrite();
  }
}

void Driver::submitDMAWrite() {
  auto &iter = dmaWriteQueue.front();

  dmaWritePending = true;

  iter.beginAt = engine.getCurrentTick();
  iter.finishedAt = iter.beginAt + SimpleSSD::PCIExpress::calculateDelay(
                                       pcieGen, pcieLane, iter.size);

  if (iter.buffer) {
    memcpy((uint8_t *)iter.addr, iter.buffer, iter.size);
  }

  engine.scheduleEvent(dmaWriteEvent, iter.finishedAt);
}

void Driver::updateInterrupt(uint16_t iv, bool post) {
  uint32_t cqdata[4];

  if (post) {
    uint64_t tick = engine.getCurrentTick();
    uint16_t count = 0;
    Queue *queue = nullptr;

    if (iv == 0) {
      queue = adminCQ;
    }
    else if (iv == 1 && ioCQ) {
      queue = ioCQ;
    }
    else {
      SimpleSSD::panic("I/O Completion Queue is not initialized");
    }

    // Peek queue for count how many requests are finished
    while (true) {
      queue->peekData((uint8_t *)cqdata, 16);

      // Check phase tag
      if (((cqdata[3] >> 16) & 0x01) == phase) {
        bool found = false;

        queue->incrTail();
        count++;

        // Search pending command list
        for (auto iter = pendingCommandList.begin();
             iter != pendingCommandList.end(); iter++) {
          if (iter->iv == iv && iter->cid == (cqdata[3] & 0xFFFF)) {
            iter->callback((uint16_t)(cqdata[3] >> 17), cqdata[0],
                           iter->context);

            pendingCommandList.erase(iter);
            found = true;

            break;
          }
        }

        if (found) {
          queue->incrHead();

          if (queue->getHead() == 0) {
            // Inverted
            phase = !phase;
          }
        }
        else {
          SimpleSSD::panic("Invalid interrupt");
        }
      }
      else {
        if (count > 0) {
          pController->ringCQHeadDoorbell(iv, queue->getHead(), tick);
        }

        break;
      }
    }
  }
}

void Driver::getVendorID(uint16_t &vid, uint16_t &ssvid) {
  // Copied from SimpleSSD-FullSystem
  vid = 0x144D;
  ssvid = 0x8086;
}

}  // namespace NVMe

}  // namespace SIL
