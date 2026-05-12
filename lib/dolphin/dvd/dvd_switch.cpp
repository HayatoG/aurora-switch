#include <aurora/dvd.h>
#include <dolphin/dvd.h>

#include "../../logging.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <bzlib.h>
#include <lzma.h>
#include <mbedtls/aes.h>
#include <zlib.h>
#include <zstd.h>

namespace {
static aurora::Module Log("aurora::dvd");

enum class DVDSource {
  None,
  Directory,
  RawImage,
};

constexpr u32 GCZ_MAGIC = 0xB10BC001;
constexpr u32 CISO_MAGIC = 0x4F534943;
constexpr u32 TGC_MAGIC = 0xA2380FAE;
constexpr u32 WBFS_MAGIC = 0x53464257;
constexpr u32 WIA_MAGIC = 0x01414957;
constexpr u32 RVZ_MAGIC = 0x015A5652;
constexpr u32 NFS_MAGIC = 0x53474745;
constexpr size_t CISO_HEADER_SIZE = 0x8000;
constexpr size_t CISO_MAP_SIZE = CISO_HEADER_SIZE - sizeof(u32) - sizeof(u32);
constexpr u64 GCZ_UNCOMPRESSED_FLAG = 1ULL << 63;
constexpr u64 DISC_SECTOR_SIZE = 0x8000;
constexpr u32 WII_SECTOR_COUNT = 143432 * 2;
constexpr u64 NFS_BLOCK_SIZE = 0x8000;
constexpr u64 NFS_MAX_FILE_SIZE = 0xFA00000;
constexpr u32 WIA_COMPRESSED_BIT = 1u << 31;
constexpr size_t LFG_K = 521;
constexpr size_t LFG_J = 32;
constexpr size_t LFG_SEED_SIZE = 17;
constexpr size_t LFG_K_BYTES = LFG_K * sizeof(u32);

struct FSTEntry {
  std::string name;
  std::filesystem::path path;
  bool isDir = false;
  u32 parent = 0;
  u32 nextOrLength = 0;
  u32 imageOffset = 0;
};

u32 readBE32(const u8* data) {
  return (static_cast<u32>(data[0]) << 24) | (static_cast<u32>(data[1]) << 16) |
         (static_cast<u32>(data[2]) << 8) | static_cast<u32>(data[3]);
}

u16 swap16(u16 value) {
  return static_cast<u16>((value << 8) | (value >> 8));
}

u32 swap32(u32 value) {
  return ((value & 0x000000ff) << 24) | ((value & 0x0000ff00) << 8) | ((value & 0x00ff0000) >> 8) |
         ((value & 0xff000000) >> 24);
}

u64 swap64(u64 value) {
  return (static_cast<u64>(swap32(static_cast<u32>(value))) << 32) |
         static_cast<u64>(swap32(static_cast<u32>(value >> 32)));
}

u32 be32(u32 value) { return swap32(value); }
u64 be64(u64 value) { return swap64(value); }
u64 alignDown(u64 value, u64 alignment) { return value & ~(alignment - 1); }
u64 alignUp(u64 value, u64 alignment) { return (value + alignment - 1) & ~(alignment - 1); }

bool readFileAt(FILE* file, u64 offset, void* out, size_t length) {
  if (file == nullptr || out == nullptr) {
    return false;
  }
  if (length == 0) {
    return true;
  }
  if (std::fseek(file, static_cast<long>(offset), SEEK_SET) != 0) {
    return false;
  }
  return std::fread(out, 1, length, file) == length;
}

u64 fileSize(FILE* file) {
  if (file == nullptr) {
    return 0;
  }
  const long current = std::ftell(file);
  if (current < 0 || std::fseek(file, 0, SEEK_END) != 0) {
    return 0;
  }
  const long end = std::ftell(file);
  std::fseek(file, current, SEEK_SET);
  return end < 0 ? 0 : static_cast<u64>(end);
}

void replaceBytes(u64 offset, u64 size, u8* out, u64 replaceOffset, u64 replaceSize, const u8* replace) {
  if (out == nullptr || replace == nullptr) {
    return;
  }
  const u64 replaceStart = std::max(offset, replaceOffset);
  const u64 replaceEnd = std::min(offset + size, replaceOffset + replaceSize);
  if (replaceEnd > replaceStart) {
    std::memcpy(out + (replaceStart - offset), replace + (replaceStart - replaceOffset),
                static_cast<size_t>(replaceEnd - replaceStart));
  }
}

template <typename T>
void replaceValue(u64 offset, u64 size, u8* out, u64 replaceOffset, const T& value) {
  replaceBytes(offset, size, out, replaceOffset, sizeof(T), reinterpret_cast<const u8*>(&value));
}

class ImageReader {
public:
  virtual ~ImageReader() = default;
  virtual bool read(u64 offset, void* out, size_t length) = 0;
  virtual u64 size() const = 0;
};

class FileImageReader : public ImageReader {
public:
  ~FileImageReader() override {
    if (m_file != nullptr) {
      std::fclose(m_file);
      m_file = nullptr;
    }
  }

  bool read(u64 offset, void* out, size_t length) override {
    if (offset + length < offset || offset + length > size()) {
      return false;
    }
    return readFileAt(m_file, offset, out, length);
  }

  u64 size() const override { return m_size; }

protected:
  explicit FileImageReader(FILE* file) : m_file(file), m_size(fileSize(file)) {}

  FILE* m_file = nullptr;
  u64 m_size = 0;
};

class RawImageReader final : public FileImageReader {
public:
  static std::unique_ptr<RawImageReader> create(const std::filesystem::path& imagePath) {
    FILE* file = std::fopen(imagePath.generic_string().c_str(), "rb");
    if (file == nullptr) {
      return nullptr;
    }
    return std::unique_ptr<RawImageReader>(new RawImageReader(file));
  }

private:
  explicit RawImageReader(FILE* file) : FileImageReader(file) {}
};

#pragma pack(push, 1)
struct GczHeader {
  u32 magicCookie;
  u32 subType;
  u64 compressedDataSize;
  u64 dataSize;
  u32 blockSize;
  u32 numBlocks;
};

struct TgcHeader {
  u32 magic;
  u32 unknown1;
  u32 tgcHeaderSize;
  u32 discHeaderAreaSize;
  u32 fstRealOffset;
  u32 fstSize;
  u32 fstMaxSize;
  u32 dolRealOffset;
  u32 dolSize;
  u32 fileAreaRealOffset;
  u32 unknown2;
  u32 unknown3;
  u32 unknown4;
  u32 fileAreaVirtualOffset;
};
#pragma pack(pop)

class GczImageReader final : public FileImageReader {
public:
  static std::unique_ptr<GczImageReader> create(const std::filesystem::path& imagePath) {
    FILE* file = std::fopen(imagePath.generic_string().c_str(), "rb");
    if (file == nullptr) {
      return nullptr;
    }

    GczHeader header{};
    if (!readFileAt(file, 0, &header, sizeof(header)) || header.magicCookie != GCZ_MAGIC || header.blockSize == 0 ||
        header.numBlocks == 0 || header.dataSize == 0 ||
        header.numBlocks != (header.dataSize + header.blockSize - 1) / header.blockSize) {
      std::fclose(file);
      return nullptr;
    }

    std::vector<u64> blockPointers(header.numBlocks);
    if (!readFileAt(file, sizeof(header), blockPointers.data(), blockPointers.size() * sizeof(u64))) {
      std::fclose(file);
      return nullptr;
    }

    return std::unique_ptr<GczImageReader>(new GczImageReader(file, header, std::move(blockPointers)));
  }

  bool read(u64 offset, void* out, size_t length) override {
    if (out == nullptr || offset + length < offset || offset + length > m_header.dataSize) {
      return false;
    }

    auto* outBytes = static_cast<u8*>(out);
    size_t remaining = length;
    while (remaining != 0) {
      const u64 block = offset / m_header.blockSize;
      const u64 blockOffset = offset % m_header.blockSize;
      const size_t copySize =
          static_cast<size_t>(std::min<u64>(m_header.blockSize - blockOffset, remaining));
      if (!loadBlock(block)) {
        return false;
      }
      std::memcpy(outBytes, m_blockCache.data() + blockOffset, copySize);
      outBytes += copySize;
      offset += copySize;
      remaining -= copySize;
    }
    return true;
  }

  u64 size() const override { return m_header.dataSize; }

private:
  GczImageReader(FILE* file, const GczHeader& header, std::vector<u64> blockPointers)
      : FileImageReader(file), m_header(header), m_blockPointers(std::move(blockPointers)),
        m_dataOffset(sizeof(GczHeader) + m_blockPointers.size() * (sizeof(u64) + sizeof(u32))),
        m_zlibBuffer(m_header.blockSize + 64), m_blockCache(m_header.blockSize) {}

  bool loadBlock(u64 block) {
    if (block == m_cachedBlock) {
      return true;
    }
    if (block >= m_blockPointers.size()) {
      return false;
    }

    const u64 rawStart = m_blockPointers[block];
    const bool uncompressed = (rawStart & GCZ_UNCOMPRESSED_FLAG) != 0;
    const u64 start = rawStart & ~GCZ_UNCOMPRESSED_FLAG;
    const u64 rawEnd = block + 1 < m_blockPointers.size() ? m_blockPointers[block + 1] : m_header.compressedDataSize;
    const u64 end = rawEnd & ~GCZ_UNCOMPRESSED_FLAG;
    if (end < start || end - start > m_zlibBuffer.size()) {
      return false;
    }

    const size_t compressedSize = static_cast<size_t>(end - start);
    if (!readFileAt(m_file, m_dataOffset + start, m_zlibBuffer.data(), compressedSize)) {
      return false;
    }

    if (uncompressed) {
      if (compressedSize != m_header.blockSize) {
        return false;
      }
      std::memcpy(m_blockCache.data(), m_zlibBuffer.data(), compressedSize);
    } else {
      z_stream stream{};
      stream.next_in = m_zlibBuffer.data();
      stream.avail_in = static_cast<uInt>(compressedSize);
      stream.next_out = m_blockCache.data();
      stream.avail_out = m_header.blockSize;
      if (inflateInit(&stream) != Z_OK) {
        return false;
      }
      const int status = inflate(&stream, Z_FULL_FLUSH);
      const bool ok = status == Z_STREAM_END && stream.avail_out == 0;
      inflateEnd(&stream);
      if (!ok) {
        return false;
      }
    }

    m_cachedBlock = block;
    return true;
  }

  GczHeader m_header{};
  std::vector<u64> m_blockPointers;
  u64 m_dataOffset = 0;
  std::vector<u8> m_zlibBuffer;
  std::vector<u8> m_blockCache;
  u64 m_cachedBlock = std::numeric_limits<u64>::max();
};

class CisoImageReader final : public FileImageReader {
public:
  static std::unique_ptr<CisoImageReader> create(const std::filesystem::path& imagePath) {
    FILE* file = std::fopen(imagePath.generic_string().c_str(), "rb");
    if (file == nullptr) {
      return nullptr;
    }

    u32 magic = 0;
    u32 blockSize = 0;
    std::vector<u8> rawMap(CISO_MAP_SIZE);
    if (!readFileAt(file, 0, &magic, sizeof(magic)) || magic != CISO_MAGIC ||
        !readFileAt(file, sizeof(magic), &blockSize, sizeof(blockSize)) || blockSize == 0 ||
        !readFileAt(file, sizeof(magic) + sizeof(blockSize), rawMap.data(), rawMap.size())) {
      std::fclose(file);
      return nullptr;
    }

    std::vector<u16> blockMap(CISO_MAP_SIZE);
    u16 usedBlockCount = 0;
    for (size_t i = 0; i < rawMap.size(); ++i) {
      blockMap[i] = rawMap[i] == 1 ? usedBlockCount++ : std::numeric_limits<u16>::max();
    }

    return std::unique_ptr<CisoImageReader>(new CisoImageReader(file, blockSize, std::move(blockMap)));
  }

  bool read(u64 offset, void* out, size_t length) override {
    if (out == nullptr || offset + length < offset || offset + length > size()) {
      return false;
    }

    auto* outBytes = static_cast<u8*>(out);
    size_t remaining = length;
    while (remaining != 0) {
      const u64 block = offset / m_blockSize;
      const u64 blockOffset = offset % m_blockSize;
      const size_t readSize = static_cast<size_t>(std::min<u64>(m_blockSize - blockOffset, remaining));
      if (block < m_blockMap.size() && m_blockMap[block] != std::numeric_limits<u16>::max()) {
        const u64 fileOffset = CISO_HEADER_SIZE + static_cast<u64>(m_blockMap[block]) * m_blockSize + blockOffset;
        if (!readFileAt(m_file, fileOffset, outBytes, readSize)) {
          return false;
        }
      } else {
        std::memset(outBytes, 0, readSize);
      }
      outBytes += readSize;
      offset += readSize;
      remaining -= readSize;
    }
    return true;
  }

  u64 size() const override { return static_cast<u64>(m_blockMap.size()) * m_blockSize; }

private:
  CisoImageReader(FILE* file, u32 blockSize, std::vector<u16> blockMap)
      : FileImageReader(file), m_blockSize(blockSize), m_blockMap(std::move(blockMap)) {}

  u32 m_blockSize = 0;
  std::vector<u16> m_blockMap;
};

class TgcImageReader final : public FileImageReader {
public:
  static std::unique_ptr<TgcImageReader> create(const std::filesystem::path& imagePath) {
    FILE* file = std::fopen(imagePath.generic_string().c_str(), "rb");
    if (file == nullptr) {
      return nullptr;
    }

    TgcHeader header{};
    if (!readFileAt(file, 0, &header, sizeof(header)) || header.magic != TGC_MAGIC) {
      std::fclose(file);
      return nullptr;
    }

    return std::unique_ptr<TgcImageReader>(new TgcImageReader(file, header));
  }

  bool read(u64 offset, void* out, size_t length) override {
    if (out == nullptr || offset + length < offset || offset + length > size()) {
      return false;
    }
    const u32 tgcHeaderSize = swap32(m_header.tgcHeaderSize);
    auto* outBytes = static_cast<u8*>(out);
    if (!readFileAt(m_file, offset + tgcHeaderSize, outBytes, length)) {
      return false;
    }

    const u32 replacementDolOffset = swap32(swap32(m_header.dolRealOffset) - tgcHeaderSize);
    const u32 replacementFstOffset = swap32(swap32(m_header.fstRealOffset) - tgcHeaderSize);
    replaceValue(offset, length, outBytes, 0x420, replacementDolOffset);
    replaceValue(offset, length, outBytes, 0x424, replacementFstOffset);
    replaceBytes(offset, length, outBytes, swap32(replacementFstOffset), m_fst.size(), m_fst.data());
    return true;
  }

  u64 size() const override {
    const u32 tgcHeaderSize = swap32(m_header.tgcHeaderSize);
    return m_size > tgcHeaderSize ? m_size - tgcHeaderSize : 0;
  }

private:
  TgcImageReader(FILE* file, const TgcHeader& header) : FileImageReader(file), m_header(header) {
    const u32 fstOffset = swap32(m_header.fstRealOffset);
    const u32 fstSize = swap32(m_header.fstSize);
    if (fstSize == 0 || fstSize > 64 * 1024 * 1024) {
      return;
    }

    m_fst.resize(fstSize);
    if (!readFileAt(m_file, fstOffset, m_fst.data(), m_fst.size()) || m_fst.size() < 12) {
      m_fst.clear();
      return;
    }

    const u32 fileAreaShift =
        swap32(m_header.fileAreaRealOffset) - swap32(m_header.fileAreaVirtualOffset) - swap32(m_header.tgcHeaderSize);
    const size_t fstEntries = std::min<size_t>(readBE32(m_fst.data() + 8), m_fst.size() / 12);
    for (size_t i = 0; i < fstEntries; ++i) {
      if (m_fst[i * 12] != 0) {
        continue;
      }
      const u32 oldOffset = readBE32(m_fst.data() + i * 12 + 4);
      const u32 newOffset = swap32(oldOffset + fileAreaShift);
      replaceValue(0, m_fst.size(), m_fst.data(), i * 12 + 4, newOffset);
    }
  }

  TgcHeader m_header{};
  std::vector<u8> m_fst;
};

class WbfsImageReader final : public ImageReader {
public:
  ~WbfsImageReader() override {
    for (auto& file : m_files) {
      if (file.file != nullptr) {
        std::fclose(file.file);
        file.file = nullptr;
      }
    }
  }

  static std::unique_ptr<WbfsImageReader> create(const std::filesystem::path& imagePath) {
    FILE* file = std::fopen(imagePath.generic_string().c_str(), "rb");
    if (file == nullptr) {
      return nullptr;
    }
    auto reader = std::unique_ptr<WbfsImageReader>(new WbfsImageReader());
    if (!reader->addFile(file) || !reader->openAdditionalFiles(imagePath) || !reader->readHeader()) {
      return nullptr;
    }
    return reader;
  }

  bool read(u64 offset, void* out, size_t length) override {
    if (out == nullptr || offset + length < offset || offset + length > size()) {
      return false;
    }

    auto* outBytes = static_cast<u8*>(out);
    size_t remaining = length;
    while (remaining != 0) {
      const u64 block = offset / m_wbfsSectorSize;
      const u64 blockOffset = offset & (m_wbfsSectorSize - 1);
      const size_t copySize = static_cast<size_t>(std::min<u64>(m_wbfsSectorSize - blockOffset, remaining));

      if (block >= m_wlbaTable.size() || m_wlbaTable[block] == 0) {
        std::memset(outBytes, 0, copySize);
      } else {
        const u64 physicalOffset = static_cast<u64>(m_wlbaTable[block]) * m_wbfsSectorSize + blockOffset;
        if (!readPhysicalAt(physicalOffset, outBytes, copySize)) {
          return false;
        }
      }

      outBytes += copySize;
      offset += copySize;
      remaining -= copySize;
    }
    return true;
  }

  u64 size() const override { return WII_SECTOR_COUNT * DISC_SECTOR_SIZE; }

private:
  struct FilePart {
    FILE* file = nullptr;
    u64 base = 0;
    u64 size = 0;
  };

  bool addFile(FILE* file) {
    const u64 size = fileSize(file);
    if (size == 0) {
      std::fclose(file);
      return false;
    }
    m_files.push_back({file, m_rawSize, size});
    m_rawSize += size;
    return true;
  }

  bool openAdditionalFiles(const std::filesystem::path& imagePath) {
    std::string path = imagePath.generic_string();
    if (path.size() < 4) {
      return true;
    }
    for (size_t i = 1; i < 10; ++i) {
      std::string nextPath = path;
      nextPath.back() = static_cast<char>('0' + i);
      FILE* next = std::fopen(nextPath.c_str(), "rb");
      if (next == nullptr) {
        break;
      }
      if (!addFile(next)) {
        return false;
      }
    }
    return true;
  }

  bool readHeader() {
    if (m_files.empty()) {
      return false;
    }

#pragma pack(push, 1)
    struct WbfsHeader {
      u32 magic;
      u32 hdSectorCount;
      u8 hdSectorShift;
      u8 wbfsSectorShift;
      u8 padding[2];
      u8 discTable[500];
    };
#pragma pack(pop)

    WbfsHeader header{};
    if (!readFileAt(m_files[0].file, 0, &header, sizeof(header)) || header.magic != WBFS_MAGIC) {
      return false;
    }

    m_hdSectorSize = 1ull << header.hdSectorShift;
    m_wbfsSectorSize = 1ull << header.wbfsSectorShift;
    const u64 expectedRawSize = static_cast<u64>(be32(header.hdSectorCount)) * m_hdSectorSize;
    if (m_hdSectorSize == 0 || m_wbfsSectorSize < DISC_SECTOR_SIZE || expectedRawSize != m_rawSize ||
        header.discTable[0] == 0) {
      return false;
    }

    m_blocksPerDisc = alignUp(WII_SECTOR_COUNT * DISC_SECTOR_SIZE, m_wbfsSectorSize) / m_wbfsSectorSize;
    m_wlbaTable.resize(m_blocksPerDisc);
    if (!readFileAt(m_files[0].file, m_hdSectorSize + 0x100, m_wlbaTable.data(),
                    m_wlbaTable.size() * sizeof(u16))) {
      return false;
    }
    for (auto& entry : m_wlbaTable) {
      entry = swap16(entry);
    }
    return true;
  }

  bool readPhysicalAt(u64 offset, void* out, size_t length) {
    auto* outBytes = static_cast<u8*>(out);
    size_t remaining = length;
    while (remaining != 0) {
      auto it = std::find_if(m_files.begin(), m_files.end(), [offset](const FilePart& part) {
        return offset >= part.base && offset < part.base + part.size;
      });
      if (it == m_files.end()) {
        return false;
      }
      const u64 fileOffset = offset - it->base;
      const size_t chunk = static_cast<size_t>(std::min<u64>(remaining, it->size - fileOffset));
      if (!readFileAt(it->file, fileOffset, outBytes, chunk)) {
        return false;
      }
      outBytes += chunk;
      offset += chunk;
      remaining -= chunk;
    }
    return true;
  }

  std::vector<FilePart> m_files;
  std::vector<u16> m_wlbaTable;
  u64 m_rawSize = 0;
  u64 m_hdSectorSize = 0;
  u64 m_wbfsSectorSize = 0;
  u64 m_blocksPerDisc = 0;
};

#pragma pack(push, 1)
struct WiaFileHeader {
  u32 magic;
  u32 version;
  u32 versionCompatible;
  u32 discSize;
  u8 discHash[20];
  u64 isoFileSize;
  u64 wiaFileSize;
  u8 fileHeadHash[20];
};

struct WiaDisc {
  u32 discType;
  u32 compression;
  s32 compressionLevel;
  u32 chunkSize;
  u8 discHead[0x80];
  u32 numPartitions;
  u32 partitionTypeSize;
  u64 partitionOffset;
  u8 partitionHash[20];
  u32 numRawData;
  u64 rawDataOffset;
  u32 rawDataSize;
  u32 numGroups;
  u64 groupOffset;
  u32 groupSize;
  u8 comprDataLen;
  u8 comprData[7];
};

struct WiaRawData {
  u64 rawDataOffset;
  u64 rawDataSize;
  u32 groupIndex;
  u32 numGroups;
};

struct WiaGroup {
  u32 dataOffset;
  u32 dataSize;
};

struct RvzGroup {
  u32 dataOffset;
  u32 dataSizeAndFlag;
  u32 rvzPackedSize;
};
#pragma pack(pop)

static_assert(sizeof(WiaFileHeader) == 0x48);
static_assert(sizeof(WiaDisc) == 0xdc);
static_assert(sizeof(WiaRawData) == 0x18);
static_assert(sizeof(WiaGroup) == 0x08);
static_assert(sizeof(RvzGroup) == 0x0c);

enum class WiaCompression : u32 {
  None = 0,
  Purge = 1,
  Bzip2 = 2,
  Lzma = 3,
  Lzma2 = 4,
  Zstandard = 5,
};

class LaggedFibonacci {
public:
  bool initWithSeedBytes(const u8* bytes, size_t length) {
    if (bytes == nullptr || length < LFG_SEED_SIZE * sizeof(u32)) {
      return false;
    }
    for (size_t i = 0; i < LFG_SEED_SIZE; ++i) {
      m_buffer[i] = readBE32(bytes + i * sizeof(u32));
    }
    m_position = 0;
    init();
    return true;
  }

  void skip(size_t bytes) {
    m_position += bytes;
    while (m_position >= LFG_K_BYTES) {
      forward();
      m_position -= LFG_K_BYTES;
    }
  }

  void fill(u8* out, size_t length) {
    while (length != 0) {
      while (m_position >= LFG_K_BYTES) {
        forward();
        m_position -= LFG_K_BYTES;
      }
      const auto* bytes = reinterpret_cast<const u8*>(m_buffer.data());
      const size_t chunk = std::min(length, LFG_K_BYTES - m_position);
      std::memcpy(out, bytes + m_position, chunk);
      out += chunk;
      length -= chunk;
      m_position += chunk;
    }
  }

private:
  void init() {
    for (size_t i = LFG_SEED_SIZE; i < LFG_K; ++i) {
      m_buffer[i] = (m_buffer[i - LFG_SEED_SIZE] << 23) ^ (m_buffer[i - LFG_SEED_SIZE + 1] >> 9) ^
                    m_buffer[i - 1];
    }
    for (auto& value : m_buffer) {
      value = swap32((value & 0xff00ffff) | ((value >> 2) & 0x00ff0000));
    }
    for (size_t i = 0; i < 4; ++i) {
      forward();
    }
  }

  void forward() {
    for (size_t i = 0; i < LFG_J; ++i) {
      m_buffer[i] ^= m_buffer[i + LFG_K - LFG_J];
    }
    for (size_t i = LFG_J; i < LFG_K; ++i) {
      m_buffer[i] ^= m_buffer[i - LFG_J];
    }
  }

  std::array<u32, LFG_K> m_buffer{};
  size_t m_position = 0;
};

bool decodeLzmaProps(const u8* props, size_t propsLen, lzma_options_lzma* options) {
  if (props == nullptr || propsLen != 5 || options == nullptr || lzma_lzma_preset(options, LZMA_PRESET_DEFAULT)) {
    return false;
  }
  u32 value = props[0];
  if (value >= 9 * 5 * 5) {
    return false;
  }
  options->lc = value % 9;
  value /= 9;
  options->pb = value / 5;
  options->lp = value % 5;
  options->dict_size = static_cast<u32>(props[1]) | (static_cast<u32>(props[2]) << 8) |
                       (static_cast<u32>(props[3]) << 16) | (static_cast<u32>(props[4]) << 24);
  return true;
}

bool decodeLzma2Props(const u8* props, size_t propsLen, lzma_options_lzma* options) {
  if (props == nullptr || propsLen != 1 || options == nullptr || lzma_lzma_preset(options, LZMA_PRESET_DEFAULT)) {
    return false;
  }
  const u32 value = props[0];
  if (value > 40) {
    return false;
  }
  options->dict_size = value == 40 ? UINT32_MAX : ((2 | (value & 1)) << (value / 2 + 11));
  return true;
}

bool decompressLzmaRaw(lzma_vli filterId, const u8* props, size_t propsLen, const u8* in, size_t inSize, u8* out,
                       size_t outCapacity, size_t* outSize) {
  lzma_options_lzma options{};
  if ((filterId == LZMA_FILTER_LZMA1 && !decodeLzmaProps(props, propsLen, &options)) ||
      (filterId == LZMA_FILTER_LZMA2 && !decodeLzma2Props(props, propsLen, &options))) {
    return false;
  }

  lzma_filter filters[2] = {{filterId, &options}, {LZMA_VLI_UNKNOWN, nullptr}};
  size_t inPos = 0;
  size_t outPos = 0;
  const lzma_ret ret = lzma_raw_buffer_decode(filters, nullptr, in, &inPos, inSize, out, &outPos, outCapacity);
  if (ret != LZMA_OK || inPos != inSize) {
    return false;
  }
  if (outSize != nullptr) {
    *outSize = outPos;
  }
  return true;
}

bool decompressWia(WiaCompression compression, const u8* props, size_t propsLen, const u8* in, size_t inSize, u8* out,
                   size_t outCapacity, size_t* outSize) {
  if (out == nullptr || (in == nullptr && inSize != 0)) {
    return false;
  }

  switch (compression) {
  case WiaCompression::None:
  case WiaCompression::Purge:
    if (inSize > outCapacity) {
      return false;
    }
    std::memcpy(out, in, inSize);
    if (outSize != nullptr) {
      *outSize = inSize;
    }
    return true;
  case WiaCompression::Bzip2: {
    unsigned int len = static_cast<unsigned int>(outCapacity);
    if (outCapacity > std::numeric_limits<unsigned int>::max() || inSize > std::numeric_limits<unsigned int>::max()) {
      return false;
    }
    const int ret = BZ2_bzBuffToBuffDecompress(reinterpret_cast<char*>(out), &len, const_cast<char*>(
                                                   reinterpret_cast<const char*>(in)),
                                               static_cast<unsigned int>(inSize), 0, 0);
    if (ret != BZ_OK) {
      return false;
    }
    if (outSize != nullptr) {
      *outSize = len;
    }
    return true;
  }
  case WiaCompression::Lzma:
    return decompressLzmaRaw(LZMA_FILTER_LZMA1, props, propsLen, in, inSize, out, outCapacity, outSize);
  case WiaCompression::Lzma2:
    return decompressLzmaRaw(LZMA_FILTER_LZMA2, props, propsLen, in, inSize, out, outCapacity, outSize);
  case WiaCompression::Zstandard: {
    const size_t ret = ZSTD_decompress(out, outCapacity, in, inSize);
    if (ZSTD_isError(ret)) {
      return false;
    }
    if (outSize != nullptr) {
      *outSize = ret;
    }
    return true;
  }
  }
  return false;
}

class WiaImageReader final : public FileImageReader {
public:
  static std::unique_ptr<WiaImageReader> create(const std::filesystem::path& imagePath) {
    FILE* file = std::fopen(imagePath.generic_string().c_str(), "rb");
    if (file == nullptr) {
      Log.warn("WIA/RVZ open failed: fopen '{}'", imagePath.generic_string());
      return nullptr;
    }

    WiaFileHeader header{};
    if (!readFileAt(file, 0, &header, sizeof(header)) || (header.magic != WIA_MAGIC && header.magic != RVZ_MAGIC)) {
      Log.warn("WIA/RVZ open failed: invalid header '{}'", imagePath.generic_string());
      std::fclose(file);
      return nullptr;
    }

    WiaDisc disc{};
    const u32 discSize = be32(header.discSize);
    if (discSize == 0 || discSize > 1024) {
      Log.warn("WIA/RVZ open failed: invalid disc header size {} '{}'", discSize, imagePath.generic_string());
      std::fclose(file);
      return nullptr;
    }
    std::vector<u8> discBuffer(discSize);
    if (!readFileAt(file, sizeof(WiaFileHeader), discBuffer.data(), discBuffer.size())) {
      Log.warn("WIA/RVZ open failed: could not read disc header '{}'", imagePath.generic_string());
      std::fclose(file);
      return nullptr;
    }
    std::memcpy(&disc, discBuffer.data(), std::min(sizeof(disc), discBuffer.size()));

    const u32 chunkSize = be32(disc.chunkSize);
    const u32 numRawData = be32(disc.numRawData);
    const u32 numGroups = be32(disc.numGroups);
    const auto compression = static_cast<WiaCompression>(be32(disc.compression));
    if (chunkSize < DISC_SECTOR_SIZE || chunkSize % DISC_SECTOR_SIZE != 0 || numRawData == 0 || numGroups == 0 ||
        numRawData > 1024 * 1024 || numGroups > 16 * 1024 * 1024 || disc.comprDataLen > sizeof(disc.comprData)) {
      Log.warn("WIA/RVZ open failed: invalid tables chunk={} raw={} groups={} compData={} '{}'", chunkSize,
               numRawData, numGroups, static_cast<unsigned>(disc.comprDataLen), imagePath.generic_string());
      std::fclose(file);
      return nullptr;
    }

    std::vector<WiaRawData> rawData(numRawData);
    if (!readTable(file, compression, disc.comprData, disc.comprDataLen, be64(disc.rawDataOffset),
                   be32(disc.rawDataSize), rawData.data(), rawData.size() * sizeof(WiaRawData))) {
      Log.warn("WIA/RVZ open failed: raw data table offset={} size={} out={} '{}'", be64(disc.rawDataOffset),
               be32(disc.rawDataSize), rawData.size() * sizeof(WiaRawData), imagePath.generic_string());
      std::fclose(file);
      return nullptr;
    }

    std::vector<RvzGroup> groups(numGroups);
    if (header.magic == RVZ_MAGIC) {
      if (!readTable(file, compression, disc.comprData, disc.comprDataLen, be64(disc.groupOffset), be32(disc.groupSize),
                     groups.data(), groups.size() * sizeof(RvzGroup))) {
        Log.warn("RVZ open failed: group table offset={} size={} out={} '{}'", be64(disc.groupOffset),
                 be32(disc.groupSize), groups.size() * sizeof(RvzGroup), imagePath.generic_string());
        std::fclose(file);
        return nullptr;
      }
    } else {
      std::vector<WiaGroup> wiaGroups(numGroups);
      if (!readTable(file, compression, disc.comprData, disc.comprDataLen, be64(disc.groupOffset), be32(disc.groupSize),
                     wiaGroups.data(), wiaGroups.size() * sizeof(WiaGroup))) {
        Log.warn("WIA open failed: group table offset={} size={} out={} '{}'", be64(disc.groupOffset),
                 be32(disc.groupSize), wiaGroups.size() * sizeof(WiaGroup), imagePath.generic_string());
        std::fclose(file);
        return nullptr;
      }
      for (size_t i = 0; i < groups.size(); ++i) {
        groups[i].dataOffset = wiaGroups[i].dataOffset;
        groups[i].dataSizeAndFlag = swap32(be32(wiaGroups[i].dataSize) | WIA_COMPRESSED_BIT);
        groups[i].rvzPackedSize = 0;
      }
    }

    return std::unique_ptr<WiaImageReader>(
        new WiaImageReader(file, header, disc, compression, std::move(rawData), std::move(groups)));
  }

  bool read(u64 offset, void* out, size_t length) override {
    if (out == nullptr || offset + length < offset || offset + length > size()) {
      return false;
    }

    auto* outBytes = static_cast<u8*>(out);
    size_t remaining = length;
    while (remaining != 0) {
      if (offset < m_cacheStart || offset >= m_cacheStart + m_cacheSize) {
        if (!loadGroupForOffset(offset)) {
          return false;
        }
      }
      const u64 cacheOffset = offset - m_cacheStart;
      const size_t copySize = static_cast<size_t>(std::min<u64>(remaining, m_cacheSize - cacheOffset));
      std::memcpy(outBytes, m_cache.data() + cacheOffset, copySize);
      outBytes += copySize;
      offset += copySize;
      remaining -= copySize;
    }
    return true;
  }

  u64 size() const override { return be64(m_header.isoFileSize); }

private:
  struct GroupInfo {
    u32 index = 0;
    u32 sector = 0;
    u32 numSectors = 0;
    u32 size = 0;
    u64 sectionOffset = 0;
  };

  WiaImageReader(FILE* file, const WiaFileHeader& header, const WiaDisc& disc, WiaCompression compression,
                 std::vector<WiaRawData> rawData, std::vector<RvzGroup> groups)
      : FileImageReader(file), m_header(header), m_disc(disc), m_compression(compression),
        m_rawData(std::move(rawData)), m_groups(std::move(groups)), m_cache(be32(m_disc.chunkSize)) {}

  static bool readTable(FILE* file, WiaCompression compression, const u8* props, size_t propsLen, u64 offset,
                        size_t compressedSize, void* out, size_t outSize) {
    if (compressedSize == 0 || out == nullptr || outSize == 0) {
      return false;
    }
    std::vector<u8> compressed(compressedSize);
    if (!readFileAt(file, offset, compressed.data(), compressed.size())) {
      return false;
    }
    size_t actual = 0;
    return decompressWia(compression, props, propsLen, compressed.data(), compressed.size(), static_cast<u8*>(out),
                         outSize, &actual) &&
           actual == outSize;
  }

  bool findGroupInfoForSector(u32 sector, GroupInfo* out) const {
    const u32 chunkSize = be32(m_disc.chunkSize);
    const u32 sectorsPerChunk = chunkSize / DISC_SECTOR_SIZE;
    for (const auto& raw : m_rawData) {
      const u64 startOffset = alignDown(be64(raw.rawDataOffset), DISC_SECTOR_SIZE);
      const u64 endOffset = be64(raw.rawDataOffset) + be64(raw.rawDataSize);
      const u32 startSector = static_cast<u32>(startOffset / DISC_SECTOR_SIZE);
      const u32 endSector = static_cast<u32>(alignUp(endOffset, DISC_SECTOR_SIZE) / DISC_SECTOR_SIZE);
      if (sector < startSector || sector >= endSector) {
        continue;
      }

      const u32 relGroup = (sector - startSector) / sectorsPerChunk;
      const u32 groupIndex = be32(raw.groupIndex) + relGroup;
      if (groupIndex >= m_groups.size()) {
        return false;
      }
      const u32 groupSector = startSector + relGroup * sectorsPerChunk;
      const u64 groupOffset = static_cast<u64>(groupSector) * DISC_SECTOR_SIZE;
      const u32 groupSize = static_cast<u32>(std::min<u64>(endOffset - groupOffset, chunkSize));
      if (out != nullptr) {
        *out = {groupIndex, groupSector, static_cast<u32>(alignUp(groupSize, DISC_SECTOR_SIZE) / DISC_SECTOR_SIZE),
                groupSize, groupOffset};
      }
      return true;
    }
    return false;
  }

  bool rvzUnpack(const u8* data, size_t dataSize, u8* out, const GroupInfo& info, size_t* outSize) {
    size_t inPos = 0;
    size_t outPos = 0;
    LaggedFibonacci lfg;
    if (outSize != nullptr) {
      *outSize = 0;
    }
    while (inPos + 4 <= dataSize) {
      const u32 rawSize = readBE32(data + inPos);
      inPos += 4;
      const bool junk = (rawSize & WIA_COMPRESSED_BIT) != 0;
      const u32 size = rawSize & ~WIA_COMPRESSED_BIT;
      if (outPos + size > info.size) {
        if (outSize != nullptr) {
          *outSize = outPos;
        }
        return false;
      }
      if (junk) {
        if (inPos + LFG_SEED_SIZE * sizeof(u32) > dataSize ||
            !lfg.initWithSeedBytes(data + inPos, LFG_SEED_SIZE * sizeof(u32))) {
          if (outSize != nullptr) {
            *outSize = outPos;
          }
          return false;
        }
        inPos += LFG_SEED_SIZE * sizeof(u32);
        lfg.skip(static_cast<size_t>((info.sectionOffset + outPos) % DISC_SECTOR_SIZE));
        lfg.fill(out + outPos, size);
      } else {
        if (inPos + size > dataSize) {
          return false;
        }
        std::memcpy(out + outPos, data + inPos, size);
        inPos += size;
      }
      outPos += size;
    }
    if (outSize != nullptr) {
      *outSize = outPos;
    }
    return inPos == dataSize && outPos == info.size;
  }

  bool loadGroupForOffset(u64 offset) {
    GroupInfo info{};
    if (!findGroupInfoForSector(static_cast<u32>(offset / DISC_SECTOR_SIZE), &info) || info.size > m_cache.size()) {
      Log.warn("WIA/RVZ read failed: no group for offset {} cache={} chunk={}", offset, m_cache.size(),
               be32(m_disc.chunkSize));
      return false;
    }

    const auto& group = m_groups[info.index];
    const u32 groupDataSize = be32(group.dataSizeAndFlag) & ~WIA_COMPRESSED_BIT;
    const bool compressed = (be32(group.dataSizeAndFlag) & WIA_COMPRESSED_BIT) != 0;
    const u32 rvzPackedSize = be32(group.rvzPackedSize);
    std::fill(m_cache.begin(), m_cache.end(), 0);

    if (groupDataSize != 0) {
      std::vector<u8> groupData(groupDataSize);
      if (!readFileAt(m_file, static_cast<u64>(be32(group.dataOffset)) * 4, groupData.data(), groupData.size())) {
        Log.warn("WIA/RVZ read failed: could not read group {} fileOff={} size={}", info.index,
                 static_cast<u64>(be32(group.dataOffset)) * 4, groupData.size());
        return false;
      }

      std::vector<u8> unpacked;
      const u8* source = groupData.data();
      size_t sourceSize = groupData.size();
      if (compressed) {
        const size_t maxSize = rvzPackedSize != 0 ? rvzPackedSize : info.size;
        unpacked.resize(maxSize);
        size_t actual = 0;
        if (!decompressWia(m_compression, m_disc.comprData, m_disc.comprDataLen, groupData.data(), groupData.size(),
                           unpacked.data(), unpacked.size(), &actual)) {
          Log.warn("WIA/RVZ read failed: decompress group {} compressedSize={} maxOut={} rvzPacked={} comp={}",
                   info.index, groupData.size(), unpacked.size(), rvzPackedSize, static_cast<u32>(m_compression));
          return false;
        }
        unpacked.resize(actual);
        source = unpacked.data();
        sourceSize = unpacked.size();
      }

      if (rvzPackedSize != 0) {
        size_t unpackedSize = 0;
        if (!rvzUnpack(source, sourceSize, m_cache.data(), info, &unpackedSize)) {
          Log.warn("RVZ read failed: unpack group {} source={} produced={} expected={} sectionOffset={}", info.index,
                   sourceSize, unpackedSize, info.size, info.sectionOffset);
          return false;
        }
      } else {
        if (sourceSize != info.size) {
          Log.warn("WIA/RVZ read failed: group {} size mismatch source={} expected={}", info.index, sourceSize,
                   info.size);
          return false;
        }
        std::memcpy(m_cache.data(), source, sourceSize);
      }
    }

    if (info.sector == 0) {
      std::memcpy(m_cache.data(), m_disc.discHead, sizeof(m_disc.discHead));
    }

    m_cacheStart = static_cast<u64>(info.sector) * DISC_SECTOR_SIZE;
    m_cacheSize = info.size;
    return true;
  }

  WiaFileHeader m_header{};
  WiaDisc m_disc{};
  WiaCompression m_compression = WiaCompression::None;
  std::vector<WiaRawData> m_rawData;
  std::vector<RvzGroup> m_groups;
  std::vector<u8> m_cache;
  u64 m_cacheStart = std::numeric_limits<u64>::max();
  u64 m_cacheSize = 0;
};

class NfsImageReader final : public ImageReader {
public:
  ~NfsImageReader() override {
    for (auto& file : m_files) {
      if (file.file != nullptr) {
        std::fclose(file.file);
        file.file = nullptr;
      }
    }
    mbedtls_aes_free(&m_aes);
  }

  static std::unique_ptr<NfsImageReader> create(const std::filesystem::path& imagePath) {
    if (imagePath.filename().generic_string() != "hif_000000.nfs") {
      return nullptr;
    }

    FILE* file = std::fopen(imagePath.generic_string().c_str(), "rb");
    if (file == nullptr) {
      return nullptr;
    }

    auto reader = std::unique_ptr<NfsImageReader>(new NfsImageReader());
    reader->m_files.push_back({file, 0, fileSize(file)});
    if (!reader->readKey(imagePath) || !reader->readHeader() || !reader->openFiles(imagePath)) {
      return nullptr;
    }
    return reader;
  }

  bool read(u64 offset, void* out, size_t length) override {
    if (out == nullptr || offset + length < offset || offset + length > size()) {
      return false;
    }

    auto* outBytes = static_cast<u8*>(out);
    size_t remaining = length;
    while (remaining != 0) {
      const u64 block = offset / NFS_BLOCK_SIZE;
      const u64 blockOffset = offset % NFS_BLOCK_SIZE;
      if (block != m_cachedBlock) {
        if (!readAndDecryptBlock(block)) {
          return false;
        }
        m_cachedBlock = block;
      }
      const size_t copySize = static_cast<size_t>(std::min<u64>(remaining, NFS_BLOCK_SIZE - blockOffset));
      std::memcpy(outBytes, m_decryptedBlock.data() + blockOffset, copySize);
      outBytes += copySize;
      offset += copySize;
      remaining -= copySize;
    }
    return true;
  }

  u64 size() const override { return m_dataSize; }

private:
  struct Range {
    u32 startBlock = 0;
    u32 numBlocks = 0;
  };

  struct FilePart {
    FILE* file = nullptr;
    u64 base = 0;
    u64 size = 0;
  };

#pragma pack(push, 1)
  struct RawRange {
    u32 startBlock;
    u32 numBlocks;
  };

  struct Header {
    u32 magic;
    u32 version;
    u32 unknown1;
    u32 unknown2;
    u32 lbaRangeCount;
    RawRange ranges[61];
    u32 endMagic;
  };
#pragma pack(pop)

  static_assert(sizeof(Header) == 0x200);

  NfsImageReader() { mbedtls_aes_init(&m_aes); }

  bool readKey(const std::filesystem::path& imagePath) {
    const auto contentDir = imagePath.parent_path();
    if (contentDir.filename().generic_string() != "content") {
      return false;
    }
    const auto keyPath = contentDir.parent_path() / "code" / "htk.bin";
    FILE* keyFile = std::fopen(keyPath.generic_string().c_str(), "rb");
    if (keyFile == nullptr) {
      return false;
    }
    const bool ok = std::fread(m_key.data(), 1, m_key.size(), keyFile) == m_key.size();
    std::fclose(keyFile);
    return ok && mbedtls_aes_setkey_dec(&m_aes, m_key.data(), 128) == 0;
  }

  bool readHeader() {
    Header header{};
    if (!readFileAt(m_files[0].file, 0, &header, sizeof(header)) || header.magic != NFS_MAGIC) {
      return false;
    }

    const u32 count = std::min<u32>(be32(header.lbaRangeCount), 61);
    m_ranges.reserve(count);
    u64 totalBlocks = 0;
    u32 greatestBlock = 0;
    for (u32 i = 0; i < count; ++i) {
      const u32 start = be32(header.ranges[i].startBlock);
      const u32 blocks = be32(header.ranges[i].numBlocks);
      m_ranges.push_back({start, blocks});
      totalBlocks += blocks;
      greatestBlock = std::max(greatestBlock, start + blocks);
    }
    if (m_ranges.empty()) {
      return false;
    }
    m_expectedRawSize = sizeof(Header) + totalBlocks * NFS_BLOCK_SIZE;
    m_dataSize = static_cast<u64>(greatestBlock) * NFS_BLOCK_SIZE;
    return true;
  }

  bool openFiles(const std::filesystem::path& imagePath) {
    const u64 fileCount = alignUp(m_expectedRawSize, NFS_MAX_FILE_SIZE) / NFS_MAX_FILE_SIZE;
    u64 rawSize = m_files[0].size;
    for (u64 i = 1; i < fileCount; ++i) {
      char name[32]{};
      std::snprintf(name, sizeof(name), "hif_%06llu.nfs", static_cast<unsigned long long>(i));
      const auto path = imagePath.parent_path() / name;
      FILE* file = std::fopen(path.generic_string().c_str(), "rb");
      if (file == nullptr) {
        return false;
      }
      const u64 size = fileSize(file);
      m_files.push_back({file, rawSize, size});
      rawSize += size;
    }
    return rawSize >= m_expectedRawSize;
  }

  u64 physicalBlockForLogical(u64 logicalBlock) const {
    u64 physicalBlocks = 0;
    for (const auto& range : m_ranges) {
      if (logicalBlock >= range.startBlock && logicalBlock < range.startBlock + range.numBlocks) {
        return physicalBlocks + (logicalBlock - range.startBlock);
      }
      physicalBlocks += range.numBlocks;
    }
    return std::numeric_limits<u64>::max();
  }

  bool readPhysicalAt(u64 offset, void* out, size_t length) {
    auto* outBytes = static_cast<u8*>(out);
    size_t remaining = length;
    while (remaining != 0) {
      auto it = std::find_if(m_files.begin(), m_files.end(), [offset](const FilePart& part) {
        return offset >= part.base && offset < part.base + part.size;
      });
      if (it == m_files.end()) {
        return false;
      }
      const u64 fileOffset = offset - it->base;
      const size_t chunk = static_cast<size_t>(std::min<u64>(remaining, it->size - fileOffset));
      if (!readFileAt(it->file, fileOffset, outBytes, chunk)) {
        return false;
      }
      outBytes += chunk;
      offset += chunk;
      remaining -= chunk;
    }
    return true;
  }

  bool readAndDecryptBlock(u64 logicalBlock) {
    const u64 physicalBlock = physicalBlockForLogical(logicalBlock);
    if (physicalBlock == std::numeric_limits<u64>::max()) {
      m_decryptedBlock.fill(0);
    } else {
      constexpr u64 blocksPerFile = NFS_MAX_FILE_SIZE / NFS_BLOCK_SIZE;
      const u64 fileIndex = physicalBlock / blocksPerFile;
      const u64 blockInFile = physicalBlock % blocksPerFile;
      const u64 physicalOffset = fileIndex * NFS_MAX_FILE_SIZE + sizeof(Header) + blockInFile * NFS_BLOCK_SIZE;
      if (!readPhysicalAt(physicalOffset, m_encryptedBlock.data(), m_encryptedBlock.size())) {
        return false;
      }

      u8 iv[16]{};
      const u64 blockBe = swap64(logicalBlock);
      std::memcpy(iv + sizeof(iv) - sizeof(blockBe), &blockBe, sizeof(blockBe));
      if (mbedtls_aes_crypt_cbc(&m_aes, MBEDTLS_AES_DECRYPT, m_encryptedBlock.size(), iv, m_encryptedBlock.data(),
                                m_decryptedBlock.data()) != 0) {
        return false;
      }
    }

    if (logicalBlock == 0) {
      m_decryptedBlock[0x61] = 1;
    }
    return true;
  }

  std::array<u8, 16> m_key{};
  mbedtls_aes_context m_aes{};
  std::vector<FilePart> m_files;
  std::vector<Range> m_ranges;
  u64 m_expectedRawSize = 0;
  u64 m_dataSize = 0;
  std::array<u8, NFS_BLOCK_SIZE> m_encryptedBlock{};
  std::array<u8, NFS_BLOCK_SIZE> m_decryptedBlock{};
  u64 m_cachedBlock = std::numeric_limits<u64>::max();
};

std::unique_ptr<ImageReader> createImageReader(const std::filesystem::path& imagePath) {
  FILE* file = std::fopen(imagePath.generic_string().c_str(), "rb");
  if (file == nullptr) {
    return nullptr;
  }

  u32 magic = 0;
  const bool haveMagic = readFileAt(file, 0, &magic, sizeof(magic));
  std::fclose(file);
  if (!haveMagic) {
    return nullptr;
  }

  switch (magic) {
  case GCZ_MAGIC:
    return GczImageReader::create(imagePath);
  case CISO_MAGIC:
    return CisoImageReader::create(imagePath);
  case TGC_MAGIC:
    return TgcImageReader::create(imagePath);
  case WBFS_MAGIC:
    return WbfsImageReader::create(imagePath);
  case WIA_MAGIC:
  case RVZ_MAGIC:
    return WiaImageReader::create(imagePath);
  case NFS_MAGIC:
    return NfsImageReader::create(imagePath);
  default:
    return RawImageReader::create(imagePath);
  }
}

DVDSource s_source = DVDSource::None;
std::filesystem::path s_root;
std::unique_ptr<ImageReader> s_imageReader;
std::mutex s_imageReaderMutex;
std::vector<u8> s_rawFst;
std::vector<u8> s_rawDol;
std::vector<FSTEntry> s_fstEntries;
s32 s_currentDir = 0;
std::string s_currentPath = "/";
DVDDiskID s_diskID = {};
BOOL s_autoInvalidation = FALSE;
BOOL s_autoFatalMessaging = FALSE;
DVDLowCallback s_resetCoverCallback = nullptr;
bool s_initialized = false;

bool isValidEntryIndex(s32 entry) { return entry >= 0 && static_cast<size_t>(entry) < s_fstEntries.size(); }

bool isAligned(const void* addr, uintptr_t align) {
  return (reinterpret_cast<uintptr_t>(addr) & (align - 1)) == 0;
}

bool readImageAt(u32 offset, void* out, size_t length) {
  std::lock_guard<std::mutex> lock(s_imageReaderMutex);
  return s_imageReader != nullptr && s_imageReader->read(offset, out, length);
}

bool readImageBE32(u32 offset, u32* out) {
  u8 data[4]{};
  if (out == nullptr || !readImageAt(offset, data, sizeof(data))) {
    return false;
  }
  *out = readBE32(data);
  return true;
}

bool nameEqualsIgnoreCase(const std::string& lhs, const char* rhs, size_t rhsLen) {
  if (lhs.size() != rhsLen) {
    return false;
  }
  for (size_t i = 0; i < rhsLen; ++i) {
    char lc = lhs[i];
    char rc = rhs[i];
    if (lc >= 'a' && lc <= 'z') {
      lc = static_cast<char>(lc - 'a' + 'A');
    }
    if (rc >= 'a' && rc <= 'z') {
      rc = static_cast<char>(rc - 'a' + 'A');
    }
    if (lc != rc) {
      return false;
    }
  }
  return true;
}

void clearState() {
  {
    std::lock_guard<std::mutex> lock(s_imageReaderMutex);
    s_imageReader.reset();
  }
  s_source = DVDSource::None;
  s_root.clear();
  s_rawFst.clear();
  s_rawDol.clear();
  s_fstEntries.clear();
  s_currentDir = 0;
  s_currentPath = "/";
  s_diskID = {};
  s_initialized = false;
}

bool loadDolImage() {
  if (!s_rawDol.empty()) {
    return true;
  }
  if (s_source != DVDSource::RawImage) {
    return false;
  }

  constexpr u32 DOL_DISC_OFFSET_ADDR = 0x420;
  constexpr u32 DOL_HEADER_SIZE = 0x100;
  constexpr u32 DOL_MAX_SIZE = 64 * 1024 * 1024;
  constexpr u32 TEXT_COUNT = 7;
  constexpr u32 DATA_COUNT = 11;
  constexpr u32 TEXT_OFFSET_TABLE = 0x00;
  constexpr u32 DATA_OFFSET_TABLE = 0x1c;
  constexpr u32 TEXT_SIZE_TABLE = 0x90;
  constexpr u32 DATA_SIZE_TABLE = 0xac;

  u32 dolOffset = 0;
  if (!readImageBE32(DOL_DISC_OFFSET_ADDR, &dolOffset) || dolOffset == 0) {
    Log.warn("Failed to read boot DOL offset from image");
    return false;
  }

  std::array<u8, DOL_HEADER_SIZE> header{};
  if (!readImageAt(dolOffset, header.data(), header.size())) {
    Log.warn("Failed to read boot DOL header at 0x{:x}", dolOffset);
    return false;
  }

  u64 dolSize = DOL_HEADER_SIZE;
  const auto visitSection = [&](u32 offsetTable, u32 sizeTable, u32 index) -> bool {
    const u32 sectionOffset = readBE32(header.data() + offsetTable + index * sizeof(u32));
    const u32 sectionSize = readBE32(header.data() + sizeTable + index * sizeof(u32));
    if (sectionSize == 0) {
      return true;
    }
    const u64 sectionEnd = static_cast<u64>(sectionOffset) + sectionSize;
    if (sectionOffset < DOL_HEADER_SIZE || sectionEnd > DOL_MAX_SIZE) {
      Log.warn("Invalid boot DOL section offset=0x{:x} size=0x{:x}", sectionOffset, sectionSize);
      return false;
    }
    dolSize = std::max(dolSize, sectionEnd);
    return true;
  };

  for (u32 i = 0; i < TEXT_COUNT; ++i) {
    if (!visitSection(TEXT_OFFSET_TABLE, TEXT_SIZE_TABLE, i)) {
      return false;
    }
  }
  for (u32 i = 0; i < DATA_COUNT; ++i) {
    if (!visitSection(DATA_OFFSET_TABLE, DATA_SIZE_TABLE, i)) {
      return false;
    }
  }
  if (dolSize < DOL_HEADER_SIZE || dolSize > DOL_MAX_SIZE) {
    Log.warn("Invalid boot DOL size {}", dolSize);
    return false;
  }

  s_rawDol.resize(static_cast<size_t>(dolSize));
  if (!readImageAt(dolOffset, s_rawDol.data(), s_rawDol.size())) {
    Log.warn("Failed to read boot DOL at 0x{:x} size={}", dolOffset, s_rawDol.size());
    s_rawDol.clear();
    return false;
  }

  Log.info("Loaded boot DOL offset=0x{:x} size={}", dolOffset, s_rawDol.size());
  return true;
}

u32 addDirectory(const std::filesystem::path& path, const std::string& name, u32 parent) {
  const u32 entry = static_cast<u32>(s_fstEntries.size());
  s_fstEntries.push_back({name, path, true, parent, 0});

  std::vector<std::filesystem::directory_entry> children;
  std::error_code ec;
  for (const auto& child : std::filesystem::directory_iterator(path, ec)) {
    children.push_back(child);
  }
  std::sort(children.begin(), children.end(), [](const auto& a, const auto& b) {
    return a.path().filename().generic_string() < b.path().filename().generic_string();
  });

  for (const auto& child : children) {
    const auto childPath = child.path();
    const auto childName = childPath.filename().generic_string();
    if (child.is_directory(ec)) {
      addDirectory(childPath, childName, entry);
    } else if (child.is_regular_file(ec)) {
      const u32 fileEntry = static_cast<u32>(s_fstEntries.size());
      const auto size = static_cast<u32>(std::filesystem::file_size(childPath, ec));
      s_fstEntries.push_back({childName, childPath, false, entry, ec ? 0u : size});
      (void)fileEntry;
    }
  }

  s_fstEntries[entry].nextOrLength = static_cast<u32>(s_fstEntries.size());
  return entry;
}

bool loadImageFST(const std::filesystem::path& imagePath) {
  {
    std::lock_guard<std::mutex> lock(s_imageReaderMutex);
    s_imageReader = createImageReader(imagePath);
  }
  if (s_imageReader == nullptr) {
    Log.warn("Image open failed: no reader for '{}'", imagePath.generic_string());
    return false;
  }

  if (!readImageAt(0, &s_diskID, sizeof(s_diskID))) {
    Log.warn("Image open failed: could not read disk ID '{}'", imagePath.generic_string());
    return false;
  }

  u32 fstOffset = 0;
  u32 fstLength = 0;
  if (!readImageBE32(0x424, &fstOffset) || !readImageBE32(0x428, &fstLength)) {
    Log.warn("Image open failed: could not read FST metadata '{}'", imagePath.generic_string());
    return false;
  }
  if (fstOffset == 0 || fstLength < 12 || fstLength > 64 * 1024 * 1024) {
    Log.warn("Image open failed: invalid FST offset={} length={} '{}'", fstOffset, fstLength,
             imagePath.generic_string());
    return false;
  }

  u8 rootEntry[12]{};
  if (!readImageAt(fstOffset, rootEntry, sizeof(rootEntry))) {
    Log.warn("Image open failed: could not read FST root offset={} '{}'", fstOffset, imagePath.generic_string());
    return false;
  }

  const u32 rootTypeAndName = readBE32(rootEntry);
  const u32 numEntries = readBE32(rootEntry + 8);
  if ((rootTypeAndName >> 24) != 1 || numEntries == 0 || numEntries > (fstLength / 12)) {
    Log.warn("Image open failed: invalid FST root type=0x{:08x} entries={} length={} '{}'", rootTypeAndName,
             numEntries, fstLength, imagePath.generic_string());
    return false;
  }

  s_rawFst.resize(fstLength);
  if (!readImageAt(fstOffset, s_rawFst.data(), s_rawFst.size())) {
    Log.warn("Image open failed: could not read full FST offset={} length={} '{}'", fstOffset, fstLength,
             imagePath.generic_string());
    return false;
  }

  const size_t stringTableOffset = static_cast<size_t>(numEntries) * 12;
  if (stringTableOffset > s_rawFst.size()) {
    return false;
  }

  s_fstEntries.clear();
  s_fstEntries.reserve(numEntries);
  std::vector<std::pair<u32, u32>> dirStack;

  for (u32 i = 0; i < numEntries; ++i) {
    while (!dirStack.empty() && i >= dirStack.back().second) {
      dirStack.pop_back();
    }

    const u8* entryData = s_rawFst.data() + static_cast<size_t>(i) * 12;
    const u32 typeAndName = readBE32(entryData);
    const bool isDir = (typeAndName >> 24) != 0;
    const u32 nameOffset = typeAndName & 0x00ffffff;
    const u32 offsetOrParent = readBE32(entryData + 4);
    const u32 nextOrLength = readBE32(entryData + 8);

    std::string name;
    const size_t stringOffset = stringTableOffset + nameOffset;
    if (i != 0 && stringOffset < s_rawFst.size()) {
      const char* str = reinterpret_cast<const char*>(s_rawFst.data() + stringOffset);
      const size_t maxLen = s_rawFst.size() - stringOffset;
      name.assign(str, strnlen(str, maxLen));
    }

    FSTEntry out{};
    out.name = std::move(name);
    out.isDir = isDir;
    out.parent = isDir ? offsetOrParent : (dirStack.empty() ? 0 : dirStack.back().first);
    out.nextOrLength = nextOrLength;
    out.imageOffset = isDir ? 0 : offsetOrParent;
    s_fstEntries.push_back(std::move(out));

    if (isDir) {
      dirStack.emplace_back(i, nextOrLength);
    }
  }

  if (s_fstEntries.empty() || !s_fstEntries[0].isDir) {
    return false;
  }
  s_fstEntries[0].name.clear();
  s_fstEntries[0].parent = 0;
  if (s_fstEntries[0].nextOrLength < 1 || s_fstEntries[0].nextOrLength > s_fstEntries.size()) {
    s_fstEntries[0].nextOrLength = static_cast<u32>(s_fstEntries.size());
  }
  return true;
}

s32 findInDir(s32 dirEntry, const char* name, size_t nameLen) {
  if (!isValidEntryIndex(dirEntry) || !s_fstEntries[dirEntry].isDir) {
    return -1;
  }

  u32 childEnd = s_fstEntries[dirEntry].nextOrLength;
  u32 i = static_cast<u32>(dirEntry) + 1;
  while (i < childEnd && i < s_fstEntries.size()) {
    if (nameEqualsIgnoreCase(s_fstEntries[i].name, name, nameLen)) {
      return static_cast<s32>(i);
    }

    if (s_fstEntries[i].isDir) {
      u32 next = s_fstEntries[i].nextOrLength;
      i = (next > i) ? next : i + 1;
    } else {
      ++i;
    }
  }
  return -1;
}

std::string buildDirPath(s32 entryNum) {
  if (entryNum <= 0 || !isValidEntryIndex(entryNum)) {
    return "/";
  }

  std::vector<std::string> parts;
  s32 cur = entryNum;
  while (cur > 0 && isValidEntryIndex(cur)) {
    parts.push_back(s_fstEntries[cur].name);
    s32 parent = static_cast<s32>(s_fstEntries[cur].parent);
    if (parent == cur) {
      break;
    }
    cur = parent;
  }

  std::string out = "/";
  for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
    out += *it;
    out += '/';
  }
  return out;
}

void setCommandResult(DVDCommandBlock* block, s32 state, u32 transferred) {
  if (block == nullptr) {
    return;
  }
  block->state = state;
  block->transferredSize = transferred;
}

s32 stateForResult(s32 result) {
  if (result == DVD_RESULT_CANCELED) {
    return DVD_STATE_CANCELED;
  }
  if (result == DVD_RESULT_IGNORED) {
    return DVD_STATE_IGNORED;
  }
  return result >= 0 ? DVD_STATE_END : DVD_STATE_FATAL_ERROR;
}

bool isCommandBlockIdle(const DVDCommandBlock* block) {
  return block != nullptr && block->state != DVD_STATE_BUSY && block->state != DVD_STATE_WAITING;
}

void beginCommand(DVDCommandBlock* block, u32 command, void* addr, u32 length, u32 offset, DVDCBCallback callback) {
  if (block == nullptr) {
    return;
  }
  block->command = command;
  block->addr = addr;
  block->length = length;
  block->offset = offset;
  block->transferredSize = 0;
  block->callback = callback;
  block->state = DVD_STATE_BUSY;
}

void finishCommand(DVDCommandBlock* block, s32 result, u32 transferred) {
  setCommandResult(block, stateForResult(result), transferred);
}

int completeImmediateCommand(DVDCommandBlock* block, u32 command, s32 result, u32 transferred, DVDCBCallback callback) {
  beginCommand(block, command, nullptr, 0, 0, callback);
  finishCommand(block, result, transferred);
  if (callback != nullptr) {
    callback(result, block);
  }
  return TRUE;
}

s32 readFileEntry(u32 entry, void* out, s32 length, s32 offset, u32* transferredOut) {
  if (transferredOut != nullptr) {
    *transferredOut = 0;
  }
  if (!isValidEntryIndex(static_cast<s32>(entry)) || s_fstEntries[entry].isDir || out == nullptr || length < 0 ||
      offset < 0) {
    return DVD_RESULT_FATAL_ERROR;
  }
  if (length == 0) {
    return 0;
  }

  if (s_source == DVDSource::RawImage) {
    const u32 fileLength = s_fstEntries[entry].nextOrLength;
    if (static_cast<u32>(offset) > fileLength) {
      return DVD_RESULT_FATAL_ERROR;
    }
    const u32 readLength = std::min(static_cast<u32>(length), fileLength - static_cast<u32>(offset));
    const u32 imageOffset = s_fstEntries[entry].imageOffset + static_cast<u32>(offset);
    if (readLength != 0 && !readImageAt(imageOffset, out, static_cast<size_t>(readLength))) {
      return DVD_RESULT_FATAL_ERROR;
    }
    if (transferredOut != nullptr) {
      *transferredOut = readLength;
    }
    return static_cast<s32>(readLength);
  }

  FILE* file = std::fopen(s_fstEntries[entry].path.generic_string().c_str(), "rb");
  if (file == nullptr) {
    return DVD_RESULT_FATAL_ERROR;
  }
  if (std::fseek(file, offset, SEEK_SET) != 0) {
    std::fclose(file);
    return DVD_RESULT_FATAL_ERROR;
  }
  const size_t read = std::fread(out, 1, static_cast<size_t>(length), file);
  std::fclose(file);
  if (transferredOut != nullptr) {
    *transferredOut = static_cast<u32>(read);
  }
  return static_cast<s32>(read);
}

void cbForReadAsync(s32 result, DVDCommandBlock* block) {
  auto* fileInfo = reinterpret_cast<DVDFileInfo*>(reinterpret_cast<char*>(block) - offsetof(DVDFileInfo, cb));
  if (fileInfo->callback != nullptr) {
    fileInfo->callback(result, fileInfo);
  }
}

void cbForSeekAsync(s32 result, DVDCommandBlock* block) {
  auto* fileInfo = reinterpret_cast<DVDFileInfo*>(reinterpret_cast<char*>(block) - offsetof(DVDFileInfo, cb));
  if (fileInfo->callback != nullptr) {
    fileInfo->callback(result, fileInfo);
  }
}

void cbForPrepareStreamAsync(s32 result, DVDCommandBlock* block) {
  auto* fileInfo = reinterpret_cast<DVDFileInfo*>(reinterpret_cast<char*>(block) - offsetof(DVDFileInfo, cb));
  if (fileInfo->callback != nullptr) {
    fileInfo->callback(result, fileInfo);
  }
}
} // namespace

extern "C" {

bool aurora_dvd_get_disk_id(const char* disc_path, DVDDiskID* out_id) {
  if (disc_path == nullptr || out_id == nullptr) {
    return false;
  }

  const auto reader = createImageReader(disc_path);
  if (reader == nullptr) {
    return false;
  }

  DVDDiskID diskID{};
  if (!reader->read(0, &diskID, sizeof(diskID))) {
    return false;
  }

  *out_id = diskID;
  return true;
}

bool aurora_dvd_open(const char* disc_path) {
  if (disc_path == nullptr) {
    return false;
  }

  clearState();
  s_root = disc_path;
  std::error_code ec;
  if (!std::filesystem::exists(s_root, ec)) {
    return false;
  }

  if (std::filesystem::is_directory(s_root, ec)) {
    s_source = DVDSource::Directory;
    addDirectory(s_root, "", 0);
    std::memcpy(s_diskID.gameName, "GZ2E", 4);
    std::memcpy(s_diskID.company, "01", 2);
    s_diskID.diskNumber = 0;
    s_diskID.gameVersion = 0;
  } else if (std::filesystem::is_regular_file(s_root, ec)) {
    s_source = DVDSource::RawImage;
    if (!loadImageFST(s_root)) {
      clearState();
      return false;
    }
  } else {
    return false;
  }

  if (s_fstEntries.empty()) {
    clearState();
    return false;
  }

  s_currentDir = 0;
  s_currentPath = "/";
  s_initialized = true;
  return true;
}

void aurora_dvd_close(void) { clearState(); }

void DVDInit(void) {}

const u8* DVDGetDOLLocation(s32* out_size) {
  if (!loadDolImage()) {
    if (out_size != nullptr) {
      *out_size = 0;
    }
    return nullptr;
  }
  if (out_size != nullptr) {
    *out_size = static_cast<s32>(s_rawDol.size());
  }
  return s_rawDol.data();
}

int DVDReadAbsAsyncPrio(DVDCommandBlock* block, void* addr, s32 length, s32 offset, DVDCBCallback callback, s32 prio) {
  (void)prio;
  if (block == nullptr || addr == nullptr || length < 0 || offset < 0 || !isAligned(addr, 32)) {
    return FALSE;
  }

  beginCommand(block, DVD_COMMAND_READ, addr, static_cast<u32>(length), static_cast<u32>(offset), callback);
  u32 transferred = 0;
  s32 result = DVD_RESULT_FATAL_ERROR;
  if (s_source == DVDSource::RawImage && readImageAt(static_cast<u32>(offset), addr, static_cast<size_t>(length))) {
    transferred = static_cast<u32>(length);
    result = length;
  }
  finishCommand(block, result, transferred);
  if (callback != nullptr) {
    callback(result, block);
  }
  return TRUE;
}

int DVDSeekAbsAsyncPrio(DVDCommandBlock* block, s32 offset, DVDCBCallback callback, s32 prio) {
  (void)prio;
  if (block == nullptr || offset < 0) {
    return FALSE;
  }
  beginCommand(block, DVD_COMMAND_SEEK, nullptr, 0, static_cast<u32>(offset), callback);
  const s32 result = s_source == DVDSource::RawImage ? DVD_RESULT_GOOD : DVD_RESULT_IGNORED;
  finishCommand(block, result, 0);
  if (callback != nullptr) {
    callback(result, block);
  }
  return TRUE;
}

int DVDReadAbsAsyncForBS(DVDCommandBlock* block, void* addr, s32 length, s32 offset, DVDCBCallback callback) {
  return DVDReadAbsAsyncPrio(block, addr, length, offset, callback, 2);
}

int DVDReadDiskID(DVDCommandBlock* block, DVDDiskID* diskID, DVDCBCallback callback) {
  if (diskID != nullptr) {
    *diskID = s_diskID;
  }
  return completeImmediateCommand(block, DVD_COMMAND_READID, DVD_RESULT_GOOD, 0, callback);
}

int DVDPrepareStreamAbsAsync(DVDCommandBlock* block, u32 length, u32 offset, DVDCBCallback callback) {
  (void)length;
  (void)offset;
  return completeImmediateCommand(block, DVD_COMMAND_INITSTREAM, DVD_RESULT_IGNORED, 0, callback);
}

int DVDCancelStreamAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  return completeImmediateCommand(block, DVD_COMMAND_CANCELSTREAM, DVD_RESULT_CANCELED, 0, callback);
}
s32 DVDCancelStream(DVDCommandBlock* block) {
  if (block != nullptr) {
    block->state = DVD_STATE_CANCELED;
  }
  return DVD_RESULT_GOOD;
}
int DVDStopStreamAtEndAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  return completeImmediateCommand(block, DVD_COMMAND_STOP_STREAM_AT_END, DVD_RESULT_GOOD, 0, callback);
}
s32 DVDStopStreamAtEnd(DVDCommandBlock* block) {
  if (block != nullptr) {
    setCommandResult(block, DVD_STATE_END, 0);
  }
  return DVD_RESULT_GOOD;
}
int DVDGetStreamErrorStatusAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  return completeImmediateCommand(block, DVD_COMMAND_REQUEST_AUDIO_ERROR, DVD_RESULT_IGNORED, 0, callback);
}
s32 DVDGetStreamErrorStatus(DVDCommandBlock*) { return DVD_RESULT_IGNORED; }
int DVDGetStreamPlayAddrAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  return completeImmediateCommand(block, DVD_COMMAND_REQUEST_PLAY_ADDR, DVD_RESULT_IGNORED, 0, callback);
}
s32 DVDGetStreamPlayAddr(DVDCommandBlock*) { return 0; }
int DVDGetStreamStartAddrAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  return completeImmediateCommand(block, DVD_COMMAND_REQUEST_START_ADDR, DVD_RESULT_IGNORED, 0, callback);
}
s32 DVDGetStreamStartAddr(DVDCommandBlock*) { return 0; }
int DVDGetStreamLengthAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  return completeImmediateCommand(block, DVD_COMMAND_REQUEST_LENGTH, DVD_RESULT_IGNORED, 0, callback);
}
s32 DVDGetStreamLength(DVDCommandBlock*) { return 0; }
int DVDChangeDiskAsyncForBS(DVDCommandBlock* block, DVDCBCallback callback) {
  return completeImmediateCommand(block, DVD_COMMAND_BS_CHANGE_DISK, DVD_RESULT_IGNORED, 0, callback);
}
int DVDChangeDiskAsync(DVDCommandBlock* block, DVDDiskID* id, DVDCBCallback callback) {
  (void)id;
  return DVDChangeDiskAsyncForBS(block, callback);
}
s32 DVDChangeDisk(DVDCommandBlock* block, DVDDiskID* id) {
  (void)id;
  return completeImmediateCommand(block, DVD_COMMAND_BS_CHANGE_DISK, DVD_RESULT_IGNORED, 0, nullptr);
}
int DVDStopMotorAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  return completeImmediateCommand(block, DVD_COMMAND_NONE, DVD_RESULT_GOOD, 0, callback);
}
s32 DVDStopMotor(DVDCommandBlock* block) {
  if (block != nullptr) {
    setCommandResult(block, DVD_STATE_END, 0);
  }
  return DVD_RESULT_GOOD;
}
int DVDInquiryAsync(DVDCommandBlock* block, DVDDriveInfo* info, DVDCBCallback callback) {
  if (info != nullptr) {
    *info = {};
  }
  return completeImmediateCommand(block, DVD_COMMAND_INQUIRY, DVD_RESULT_GOOD, 0, callback);
}
s32 DVDInquiry(DVDCommandBlock* block, DVDDriveInfo* info) {
  DVDInquiryAsync(block, info, nullptr);
  return DVD_RESULT_GOOD;
}
void DVDReset(void) {}
int DVDResetRequired(void) { return FALSE; }
s32 DVDGetCommandBlockStatus(const DVDCommandBlock* block) {
  return block != nullptr ? block->state : DVD_STATE_FATAL_ERROR;
}
s32 DVDGetDriveStatus(void) { return s_initialized ? DVD_STATE_END : DVD_STATE_NO_DISK; }
BOOL DVDSetAutoInvalidation(BOOL autoInval) {
  BOOL old = s_autoInvalidation;
  s_autoInvalidation = autoInval;
  return old;
}
void DVDPause(void) {}
void DVDResume(void) {}
int DVDCancelAsync(DVDCommandBlock* block, DVDCBCallback callback) {
  return completeImmediateCommand(block, DVD_COMMAND_NONE, DVD_RESULT_CANCELED, 0, callback);
}
s32 DVDCancel(volatile DVDCommandBlock* block) {
  if (block != nullptr) {
    const_cast<DVDCommandBlock*>(block)->state = DVD_STATE_CANCELED;
  }
  return DVD_RESULT_GOOD;
}
int DVDCancelAllAsync(DVDCBCallback callback) {
  if (callback != nullptr) {
    callback(DVD_RESULT_CANCELED, nullptr);
  }
  return TRUE;
}
s32 DVDCancelAll(void) { return DVD_RESULT_GOOD; }
DVDDiskID* DVDGetCurrentDiskID(void) { return &s_diskID; }
BOOL DVDCheckDisk(void) { return s_initialized ? TRUE : FALSE; }
int DVDSetAutoFatalMessaging(BOOL enable) {
  BOOL old = s_autoFatalMessaging;
  s_autoFatalMessaging = enable;
  return old;
}

s32 DVDConvertPathToEntrynum(const char* pathPtr) {
  if (pathPtr == nullptr || !s_initialized) {
    return -1;
  }
  if (pathPtr[0] == '\0') {
    return s_currentDir;
  }

  s32 cur = pathPtr[0] == '/' ? 0 : s_currentDir;
  const char* ptr = pathPtr[0] == '/' ? pathPtr + 1 : pathPtr;
  while (*ptr != '\0') {
    while (*ptr == '/') {
      ++ptr;
    }
    if (*ptr == '\0') {
      break;
    }
    const char* start = ptr;
    while (*ptr != '\0' && *ptr != '/') {
      ++ptr;
    }
    const size_t len = static_cast<size_t>(ptr - start);
    if (len == 1 && start[0] == '.') {
      continue;
    }
    if (len == 2 && start[0] == '.' && start[1] == '.') {
      cur = isValidEntryIndex(cur) ? static_cast<s32>(s_fstEntries[cur].parent) : 0;
      continue;
    }
    cur = findInDir(cur, start, len);
    if (cur < 0) {
      return -1;
    }
  }
  return cur;
}

BOOL DVDFastOpen(s32 entrynum, DVDFileInfo* fileInfo) {
  if (fileInfo == nullptr || !isValidEntryIndex(entrynum) || s_fstEntries[entrynum].isDir) {
    return FALSE;
  }
  std::memset(fileInfo, 0, sizeof(*fileInfo));
  fileInfo->startAddr = s_fstEntries[entrynum].imageOffset;
  fileInfo->length = s_fstEntries[entrynum].nextOrLength;
  fileInfo->cb.state = DVD_STATE_END;
  fileInfo->cb.userData = reinterpret_cast<void*>(static_cast<uintptr_t>(entrynum));
  return TRUE;
}

BOOL DVDOpen(const char* fileName, DVDFileInfo* fileInfo) {
  const s32 entrynum = DVDConvertPathToEntrynum(fileName);
  return DVDFastOpen(entrynum, fileInfo);
}

BOOL DVDClose(DVDFileInfo* fileInfo) {
  if (fileInfo != nullptr) {
    fileInfo->cb.userData = nullptr;
  }
  return TRUE;
}

BOOL DVDGetCurrentDir(char* path, u32 maxlen) {
  if (path == nullptr || maxlen == 0) {
    return FALSE;
  }
  std::snprintf(path, maxlen, "%s", s_currentPath.c_str());
  return TRUE;
}

BOOL DVDChangeDir(const char* dirName) {
  const s32 entry = DVDConvertPathToEntrynum(dirName);
  if (!isValidEntryIndex(entry) || !s_fstEntries[entry].isDir) {
    return FALSE;
  }
  s_currentDir = entry;
  s_currentPath = buildDirPath(entry);
  return TRUE;
}

BOOL DVDReadAsyncPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, DVDCallback callback, s32 prio) {
  (void)prio;
  if (fileInfo == nullptr || addr == nullptr || length < 0 || offset < 0 || !isAligned(addr, 32)) {
    return FALSE;
  }
  fileInfo->callback = callback;
  beginCommand(&fileInfo->cb, DVD_COMMAND_READ, addr, static_cast<u32>(length), static_cast<u32>(offset),
               cbForReadAsync);
  const u32 entry = static_cast<u32>(reinterpret_cast<uintptr_t>(fileInfo->cb.userData));
  u32 transferred = 0;
  const s32 result = readFileEntry(entry, addr, length, offset, &transferred);
  finishCommand(&fileInfo->cb, result, transferred);
  if (callback != nullptr) {
    callback(result, fileInfo);
  }
  return TRUE;
}

s32 DVDReadPrio(DVDFileInfo* fileInfo, void* addr, s32 length, s32 offset, s32 prio) {
  if (!DVDReadAsyncPrio(fileInfo, addr, length, offset, nullptr, prio)) {
    return DVD_RESULT_FATAL_ERROR;
  }
  return static_cast<s32>(fileInfo->cb.transferredSize);
}

int DVDSeekAsyncPrio(DVDFileInfo* fileInfo, s32 offset, void (*callback)(s32, DVDFileInfo*), s32 prio) {
  (void)prio;
  if (fileInfo == nullptr || offset < 0 || offset > static_cast<s32>(fileInfo->length)) {
    return FALSE;
  }
  fileInfo->callback = callback;
  beginCommand(&fileInfo->cb, DVD_COMMAND_SEEK, nullptr, 0, static_cast<u32>(offset), cbForSeekAsync);
  finishCommand(&fileInfo->cb, DVD_RESULT_GOOD, 0);
  if (callback != nullptr) {
    callback(DVD_RESULT_GOOD, fileInfo);
  }
  return TRUE;
}

s32 DVDSeekPrio(DVDFileInfo* fileInfo, s32 offset, s32 prio) {
  return DVDSeekAsyncPrio(fileInfo, offset, nullptr, prio) ? DVD_RESULT_GOOD : DVD_RESULT_FATAL_ERROR;
}

s32 DVDGetFileInfoStatus(const DVDFileInfo* fileInfo) {
  return fileInfo != nullptr ? fileInfo->cb.state : DVD_STATE_FATAL_ERROR;
}

BOOL DVDFastOpenDir(s32 entrynum, DVDDir* dir) {
  if (dir == nullptr || !isValidEntryIndex(entrynum) || !s_fstEntries[entrynum].isDir) {
    return FALSE;
  }
  dir->entryNum = static_cast<u32>(entrynum);
  dir->location = static_cast<u32>(entrynum) + 1;
  dir->next = s_fstEntries[entrynum].nextOrLength;
  return TRUE;
}
int DVDOpenDir(const char* dirName, DVDDir* dir) { return DVDFastOpenDir(DVDConvertPathToEntrynum(dirName), dir); }
int DVDReadDir(DVDDir* dir, DVDDirEntry* dirent) {
  if (dir == nullptr || dirent == nullptr) {
    return FALSE;
  }
  while (dir->location < dir->next && dir->location < s_fstEntries.size()) {
    const u32 entry = dir->location;
    const auto& fst = s_fstEntries[entry];
    dir->location = fst.isDir ? fst.nextOrLength : entry + 1;
    dirent->entryNum = entry;
    dirent->isDir = fst.isDir ? TRUE : FALSE;
    dirent->name = const_cast<char*>(fst.name.c_str());
    return TRUE;
  }
  return FALSE;
}
int DVDCloseDir(DVDDir*) { return TRUE; }
void DVDRewindDir(DVDDir* dir) {
  if (dir != nullptr && isValidEntryIndex(static_cast<s32>(dir->entryNum))) {
    dir->location = dir->entryNum + 1;
  }
}
void* DVDGetFSTLocation(void) { return s_rawFst.empty() ? nullptr : s_rawFst.data(); }
BOOL DVDPrepareStreamAsync(DVDFileInfo* fileInfo, u32 length, u32 offset, DVDCallback callback) {
  fileInfo->callback = callback;
  return DVDPrepareStreamAbsAsync(&fileInfo->cb, length, offset, cbForPrepareStreamAsync);
}
s32 DVDPrepareStream(DVDFileInfo* fileInfo, u32 length, u32 offset) {
  return DVDPrepareStreamAsync(fileInfo, length, offset, nullptr) ? DVD_RESULT_IGNORED : DVD_RESULT_FATAL_ERROR;
}
s32 DVDGetTransferredSize(DVDFileInfo* fileinfo) {
  return fileinfo != nullptr ? static_cast<s32>(fileinfo->cb.transferredSize) : 0;
}

int DVDCompareDiskID(const DVDDiskID* id1, const DVDDiskID* id2) {
  if (id1 == nullptr || id2 == nullptr) {
    return FALSE;
  }
  return std::memcmp(id1, id2, sizeof(DVDDiskID)) == 0 ? TRUE : FALSE;
}
DVDDiskID* DVDGenerateDiskID(DVDDiskID* id, const char* game, const char* company, u8 diskNum, u8 version) {
  if (id == nullptr) {
    return nullptr;
  }
  std::memset(id, 0, sizeof(*id));
  if (game != nullptr) {
    std::memcpy(id->gameName, game, std::min<size_t>(4, std::strlen(game)));
  }
  if (company != nullptr) {
    std::memcpy(id->company, company, std::min<size_t>(2, std::strlen(company)));
  }
  id->diskNumber = diskNum;
  id->gameVersion = version;
  return id;
}

BOOL DVDLowRead(void* addr, u32 length, u32 offset, DVDLowCallback callback) {
  const BOOL ok = s_source == DVDSource::RawImage && addr != nullptr &&
                  readImageAt(offset, addr, static_cast<size_t>(length));
  if (callback != nullptr) {
    callback(ok ? DVD_RESULT_GOOD : DVD_RESULT_FATAL_ERROR);
  }
  return ok;
}
BOOL DVDLowSeek(u32 offset, DVDLowCallback callback) {
  (void)offset;
  if (callback != nullptr) {
    callback(DVD_RESULT_IGNORED);
  }
  return TRUE;
}
BOOL DVDLowWaitCoverClose(DVDLowCallback callback) {
  if (callback != nullptr) {
    callback(0);
  }
  return TRUE;
}
BOOL DVDLowReadDiskID(DVDDiskID* diskID, DVDLowCallback callback) {
  if (diskID != nullptr) {
    *diskID = s_diskID;
  }
  if (callback != nullptr) {
    callback(0);
  }
  return TRUE;
}
BOOL DVDLowStopMotor(DVDLowCallback callback) {
  if (callback != nullptr) {
    callback(0);
  }
  return TRUE;
}
BOOL DVDLowRequestError(DVDLowCallback callback) {
  if (callback != nullptr) {
    callback(0);
  }
  return TRUE;
}
BOOL DVDLowInquiry(DVDDriveInfo* info, DVDLowCallback callback) {
  if (info != nullptr) {
    *info = {};
  }
  if (callback != nullptr) {
    callback(0);
  }
  return TRUE;
}
BOOL DVDLowAudioStream(u32 subcmd, u32 length, u32 offset, DVDLowCallback callback) {
  (void)subcmd;
  (void)length;
  (void)offset;
  if (callback != nullptr) {
    callback(0);
  }
  return TRUE;
}
BOOL DVDLowRequestAudioStatus(u32 subcmd, DVDLowCallback callback) {
  (void)subcmd;
  if (callback != nullptr) {
    callback(0);
  }
  return TRUE;
}
BOOL DVDLowAudioBufferConfig(BOOL enable, u32 size, DVDLowCallback callback) {
  (void)enable;
  (void)size;
  if (callback != nullptr) {
    callback(0);
  }
  return TRUE;
}
void DVDLowReset(void) {
  if (s_resetCoverCallback != nullptr) {
    s_resetCoverCallback(0);
  }
}
DVDLowCallback DVDLowSetResetCoverCallback(DVDLowCallback callback) {
  DVDLowCallback old = s_resetCoverCallback;
  s_resetCoverCallback = callback;
  return old;
}
BOOL DVDLowBreak(void) { return TRUE; }
DVDLowCallback DVDLowClearCallback(void) { return nullptr; }
u32 DVDLowGetCoverStatus(void) { return s_initialized ? 0 : 1; }
void DVDDumpWaitingQueue(void) {}

} // extern "C"
