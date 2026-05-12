#include <dolphin/card.h>
#include <dolphin/exi.h>

#include <cstring>

namespace {
constexpr s32 kNoCard = CARD_RESULT_NOCARD;
constexpr s32 kReady = CARD_RESULT_READY;
constexpr u16 kMemSizeMb = 59;
constexpr u32 kSectorSize = 8192;

u16 s_vendorId = 0;
u32 s_currentMode[2] = {};
DVDDiskID s_diskIds[2] = {};
} // namespace

extern "C" {

u32 __CARDFreq = EXI_FREQ_16M;

#if TARGET_PC
void CARDInit(const char* game, const char* maker) {
  (void)game;
  (void)maker;
}
void CARDSetGameAndMaker(const s32 chan, const char* game, const char* maker) {
  (void)chan;
  (void)game;
  (void)maker;
}
void CARDDetectDolphin(s32 chan) { (void)chan; }
void CARDSetBasePath(const char* path, s32 chan) {
  (void)path;
  (void)chan;
}
void CARDSetLoadType(CARDFileType type) { (void)type; }
#else
void CARDInit(void) {}
#endif

s32 CARDGetResultCode(s32 chan) {
  (void)chan;
  return kNoCard;
}

s32 CARDFreeBlocks(s32 chan, s32* byteNotUsed, s32* filesNotUsed) {
  (void)chan;
  if (byteNotUsed != nullptr) {
    *byteNotUsed = 0;
  }
  if (filesNotUsed != nullptr) {
    *filesNotUsed = 0;
  }
  return kNoCard;
}

s32 CARDGetEncoding(s32 chan, u16* encode) {
  (void)chan;
  if (encode != nullptr) {
    *encode = CARD_ENCODE_ANSI;
  }
  return kNoCard;
}

s32 CARDGetMemSize(s32 chan, u16* size) {
  (void)chan;
  if (size != nullptr) {
    *size = kMemSizeMb;
  }
  return kNoCard;
}

s32 CARDGetSectorSize(s32 chan, u32* size) {
  (void)chan;
  if (size != nullptr) {
    *size = kSectorSize;
  }
  return kNoCard;
}

const DVDDiskID* CARDGetDiskID(s32 chan) {
  if (chan < 0 || chan >= 2) {
    return nullptr;
  }
  return &s_diskIds[chan];
}

s32 CARDSetDiskID(s32 chan, const DVDDiskID* diskID) {
  if (chan < 0 || chan >= 2 || diskID == nullptr) {
    return CARD_RESULT_FATAL_ERROR;
  }
  s_diskIds[chan] = *diskID;
  return kReady;
}

BOOL CARDSetFastMode(BOOL enable) {
  (void)enable;
  return FALSE;
}

BOOL CARDGetFastMode(void) { return FALSE; }

s32 CARDGetCurrentMode(s32 chan, u32* mode) {
  if (chan < 0 || chan >= 2) {
    return CARD_RESULT_FATAL_ERROR;
  }
  if (mode != nullptr) {
    *mode = s_currentMode[chan];
  }
  return kNoCard;
}

s32 CARDCheckExAsync(s32 chan, s32* xferBytes, CARDCallback callback) {
  if (xferBytes != nullptr) {
    *xferBytes = 0;
  }
  if (callback != nullptr) {
    callback(chan, kNoCard);
  }
  return kNoCard;
}
s32 CARDCheckAsync(s32 chan, CARDCallback callback) { return CARDCheckExAsync(chan, nullptr, callback); }
s32 CARDCheckEx(s32 chan, s32* xferBytes) { return CARDCheckExAsync(chan, xferBytes, nullptr); }
s32 CARDCheck(s32 chan) { return CARDCheckEx(chan, nullptr); }

s32 CARDCreate(s32 chan, const char* fileName, u32 size, CARDFileInfo* fileInfo) {
  (void)chan;
  (void)fileName;
  (void)size;
  (void)fileInfo;
  return kNoCard;
}
s32 CARDCreateAsync(s32 chan, const char* fileName, u32 size, CARDFileInfo* fileInfo, CARDCallback callback) {
  const s32 result = CARDCreate(chan, fileName, size, fileInfo);
  if (callback != nullptr) {
    callback(chan, result);
  }
  return result;
}

s32 CARDFastDelete(s32 chan, s32 fileNo) {
  (void)chan;
  (void)fileNo;
  return kNoCard;
}
s32 CARDFastDeleteAsync(s32 chan, s32 fileNo, CARDCallback callback) {
  const s32 result = CARDFastDelete(chan, fileNo);
  if (callback != nullptr) {
    callback(chan, result);
  }
  return result;
}
s32 CARDDelete(s32 chan, const char* fileName) {
  (void)chan;
  (void)fileName;
  return kNoCard;
}
s32 CARDDeleteAsync(s32 chan, const char* fileName, CARDCallback callback) {
  const s32 result = CARDDelete(chan, fileName);
  if (callback != nullptr) {
    callback(chan, result);
  }
  return result;
}

s32 CARDErase(CARDFileInfo* fileInfo, s32 length, s32 offset) {
  (void)fileInfo;
  (void)length;
  (void)offset;
  return kNoCard;
}
s32 CARDEraseAsync(CARDFileInfo* fileInfo, s32 length, s32 offset, CARDCallback callback) {
  const s32 chan = fileInfo != nullptr ? fileInfo->chan : -1;
  const s32 result = CARDErase(fileInfo, length, offset);
  if (callback != nullptr) {
    callback(chan, result);
  }
  return result;
}

s32 CARDFormat(s32 chan) {
  (void)chan;
  return kNoCard;
}

int CARDProbe(s32 chan) {
  (void)chan;
  return FALSE;
}
s32 CARDProbeEx(s32 chan, s32* memSize, s32* sectorSize) {
  if (memSize != nullptr) {
    *memSize = kMemSizeMb;
  }
  if (sectorSize != nullptr) {
    *sectorSize = static_cast<s32>(kSectorSize);
  }
  (void)chan;
  return kNoCard;
}
s32 CARDMount(s32 chan, void* workArea, CARDCallback detachCallback) {
  (void)workArea;
  (void)detachCallback;
  return kNoCard;
}
s32 CARDMountAsync(s32 chan, void* workArea, CARDCallback detachCallback, CARDCallback attachCallback) {
  const s32 result = CARDMount(chan, workArea, detachCallback);
  if (attachCallback != nullptr) {
    attachCallback(chan, result);
  }
  return result;
}
s32 CARDUnmount(s32 chan) {
  (void)chan;
  return kReady;
}

u16 CARDSetVendorID(u16 vendorID) {
  const u16 old = s_vendorId;
  s_vendorId = vendorID;
  return old;
}
u16 CARDGetVendorID(void) { return s_vendorId; }
s32 CARDGetSerialNo(s32 chan, u64* serialNo) {
  (void)chan;
  if (serialNo != nullptr) {
    *serialNo = 0;
  }
  return kNoCard;
}
s32 CARDGetUniqueCode(s32 chan, u64* uniqueCode) {
  (void)chan;
  if (uniqueCode != nullptr) {
    *uniqueCode = 0;
  }
  return kNoCard;
}
s32 CARDGetAttributes(s32 chan, s32 fileNo, u8* attr) {
  (void)chan;
  (void)fileNo;
  if (attr != nullptr) {
    *attr = 0;
  }
  return kNoCard;
}
s32 CARDSetAttributes(s32 chan, s32 fileNo, u8 attr) {
  (void)chan;
  (void)fileNo;
  (void)attr;
  return kNoCard;
}
s32 CARDSetAttributesAsync(s32 chan, s32 fileNo, u8 attr, CARDCallback callback) {
  const s32 result = CARDSetAttributes(chan, fileNo, attr);
  if (callback != nullptr) {
    callback(chan, result);
  }
  return result;
}

s32 CARDFastOpen(s32 chan, s32 fileNo, CARDFileInfo* fileInfo) {
  if (fileInfo != nullptr) {
    std::memset(fileInfo, 0, sizeof(*fileInfo));
    fileInfo->chan = chan;
    fileInfo->fileNo = fileNo;
  }
  return kNoCard;
}
s32 CARDOpen(s32 chan, const char* fileName, CARDFileInfo* fileInfo) {
  (void)fileName;
  return CARDFastOpen(chan, -1, fileInfo);
}
s32 CARDClose(CARDFileInfo* fileInfo) {
  (void)fileInfo;
  return kReady;
}

s32 CARDProgram(CARDFileInfo* fileInfo, void* buf, s32 length, s32 offset) {
  (void)fileInfo;
  (void)buf;
  (void)length;
  (void)offset;
  return kNoCard;
}
s32 CARDProgramAsync(CARDFileInfo* fileInfo, void* buf, s32 length, s32 offset, CARDCallback callback) {
  const s32 chan = fileInfo != nullptr ? fileInfo->chan : -1;
  const s32 result = CARDProgram(fileInfo, buf, length, offset);
  if (callback != nullptr) {
    callback(chan, result);
  }
  return result;
}

s32 CARDGetXferredBytes(s32 chan) {
  (void)chan;
  return 0;
}

s32 CARDRead(const CARDFileInfo* fileInfo, void* addr, s32 length, s32 offset) {
  (void)fileInfo;
  (void)addr;
  (void)length;
  (void)offset;
  return kNoCard;
}
s32 CARDReadAsync(const CARDFileInfo* fileInfo, void* addr, s32 length, s32 offset, CARDCallback callback) {
  const s32 chan = fileInfo != nullptr ? fileInfo->chan : -1;
  const s32 result = CARDRead(fileInfo, addr, length, offset);
  if (callback != nullptr) {
    callback(chan, result);
  }
  return result;
}
s32 CARDCancel(CARDFileInfo* fileInfo) {
  (void)fileInfo;
  return CARD_RESULT_CANCELED;
}

s32 CARDRename(s32 chan, const char* oldName, const char* newName) {
  (void)chan;
  (void)oldName;
  (void)newName;
  return kNoCard;
}
s32 CARDRenameAsync(s32 chan, const char* oldName, const char* newName, CARDCallback callback) {
  const s32 result = CARDRename(chan, oldName, newName);
  if (callback != nullptr) {
    callback(chan, result);
  }
  return result;
}

s32 CARDGetStatus(s32 chan, s32 fileNo, CARDStat* stat) {
  (void)chan;
  (void)fileNo;
  if (stat != nullptr) {
    std::memset(stat, 0, sizeof(*stat));
  }
  return kNoCard;
}
s32 CARDSetStatus(s32 chan, s32 fileNo, const CARDStat* stat) {
  (void)chan;
  (void)fileNo;
  (void)stat;
  return kNoCard;
}
s32 CARDSetStatusAsync(s32 chan, s32 fileNo, const CARDStat* stat, CARDCallback callback) {
  const s32 result = CARDSetStatus(chan, fileNo, stat);
  if (callback != nullptr) {
    callback(chan, result);
  }
  return result;
}

s32 CARDWrite(const CARDFileInfo* fileInfo, const void* addr, s32 length, s32 offset) {
  (void)fileInfo;
  (void)addr;
  (void)length;
  (void)offset;
  return kNoCard;
}
s32 CARDWriteAsync(const CARDFileInfo* fileInfo, const void* addr, s32 length, s32 offset, CARDCallback callback) {
  const s32 chan = fileInfo != nullptr ? fileInfo->chan : -1;
  const s32 result = CARDWrite(fileInfo, addr, length, offset);
  if (callback != nullptr) {
    callback(chan, result);
  }
  return result;
}

} // extern "C"
