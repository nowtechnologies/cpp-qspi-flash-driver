#ifndef NOWTECH_FLASHCONFIG
#define NOWTECH_FLASHCONFIG

#include "FlashCommon.h"
#include "PoolAllocator.h"
#include <cstdint>
#include <algorithm>

namespace nowtech::memory {

template<typename tInterface, uint32_t tPagesNeeded, FlashCopies tCopies, uint32_t tReadAheadSizeInPages, uint32_t tMaxItemCount, uint32_t tValueBufferSize = sizeof(float)>
class FlashConfig final : FlashCommon<tInterface> {
  template<typename tInterfaceOther, typename tPlugin1, typename tPlugin2, typename tPlugin3>
  friend class FlashPartitioner;

  using FlashCommon<tInterface>::cPageSizeInBytes;
  using FlashCommon<tInterface>::cSectorSizeInPages;
  using FlashCommon<tInterface>::cFlashSizeInPages;
  using FlashCommon<tInterface>::cOffsetPageMagic;
  using FlashCommon<tInterface>::cOffsetPageCount;
  using FlashCommon<tInterface>::cOffsetPageChecksum;
  using FlashCommon<tInterface>::cOffsetPageItems;
  using FlashCommon<tInterface>::cUnusedValue;
  using FlashCommon<tInterface>::calculateChecksum;

private:
  static constexpr uint16_t cOffsetItemId = 0u;
  static constexpr uint16_t cOffsetItemCount = cOffsetItemId + sizeof(uint16_t);
  static constexpr uint16_t cOffsetItemData  = cOffsetItemCount + sizeof(uint16_t);
  static constexpr uint16_t cPageItemSpace   = cPageSizeInBytes - cOffsetPageItems;
  static constexpr uint16_t cMaxItemDataSize = cPageItemSpace - cOffsetItemData;
  static constexpr uint32_t cCopySizeInPages = (tPagesNeeded / (tCopies == FlashCopies::c2 ? 2u : 1u));

  static_assert(tCopies == FlashCopies::c1 || tCopies == FlashCopies::c2, "Illegal FlashCopies value");
  static_assert(tReadAheadSizeInPages > 1u, "FlashConfig needs read ahead buffer");
  static_assert(tReadAheadSizeInPages % cSectorSizeInPages == 0u, "FlashConfig read ahead buffer must be a multiply of sector size.");
  static_assert(cCopySizeInPages % cSectorSizeInPages == 0u, "FlashConfig copies must be a multiply of the sector size.");
  static_assert(cCopySizeInPages * (tCopies == FlashCopies::c2 ? 2u : 1u) == tPagesNeeded, "Sum of copies must yield the partition size.");

  class ConfigItem final {
  private:
    enum class Status : uint8_t {
      cVoid = 0u,
      cFew  = 1u,
      cMany = 2u
    };
 
    union {                // because of C++14
      uint8_t *mDataMany;
      uint8_t mDataFew[tValueBufferSize];
    };
    uint32_t   mPageIndex;             // relative to start of this copy
    uint16_t   mDataOffsetInFirstPage; // the first real item data address just after the item header
    uint16_t   mCount;
    Status     mStatus;

  public:
    ConfigItem() noexcept : mPageIndex(0u), mCount(0u), mStatus(Status::cVoid) {
    }

    ConfigItem(ConfigItem const &aOther) = delete;
    ConfigItem(ConfigItem &&aOther) = delete;
    ConfigItem& operator=(ConfigItem const &aOther) = delete;
    ConfigItem& operator=(ConfigItem &&aOther) = delete;

    ~ConfigItem() {
      if(mStatus == Status::cMany) {
        tInterface::template _deleteArray<uint8_t>(mDataMany);
      }
      else { // nothing to do
      }
    }

    bool isValid() noexcept {
      return mCount > 0u;
    }

    void init(uint32_t const aStartPage, uint16_t const aDataOffsetInFirstPage, uint16_t const aCount) {
      if(mStatus == Status::cVoid) {
        mPageIndex = aStartPage;
        mDataOffsetInFirstPage = aDataOffsetInFirstPage;
        mCount = aCount;
        mStatus = (mCount > tValueBufferSize ? Status::cMany : Status::cFew);
        if(mCount > tValueBufferSize) {
          mDataMany = tInterface::template _newArray<uint8_t>(mCount);
        }
        else { // nothing to do
        }
      }
      else { // nothing to do
      }
    }

    uint32_t getPageIndex() const noexcept {
      return mPageIndex;
    }

    uint32_t getDataOffsetInFirstPage() const noexcept {
      return mDataOffsetInFirstPage;
    }

    uint32_t getCount() const noexcept {
      return mCount;
    }

    uint8_t const * getData() const noexcept {
      return const_cast<uint8_t const *>(mCount > tValueBufferSize ? mDataMany : mDataFew);
    }

    bool doesMatch(uint8_t const * const aData) const noexcept {
      uint8_t const * destination;
      if(mCount > tValueBufferSize) {
        destination = mDataMany;
      }
      else {
        destination = mDataFew;
      }
      return std::mismatch(aData, aData + mCount, destination).first == aData + mCount;
    }
    
    void setData(uint8_t const * const aData) noexcept {
      uint8_t* destination;
      if(mCount > tValueBufferSize) {
        destination = mDataMany;
      }
      else {
        destination = mDataFew;
      }
      std::copy_n(aData, mCount, destination);
    }
  };

  class NewDeleteOccupier final {
  public:
    NewDeleteOccupier() noexcept = default;

    void* occupy(uint32_t const aSize) {
      return reinterpret_cast<void*>(tInterface::template _newArray<uint8_t>(aSize));
    }

    void release(void* const aPointer) {
      tInterface::template _deleteArray<uint8_t>(reinterpret_cast<uint8_t*>(aPointer));
    }

    void badAlloc() {
      tInterface::badAlloc();
    }
  };

  enum class ReadResult : uint8_t {
    cOk               = 0u,
    cErrorChecksum    = 1u,
    cErrorConsistency = 2u,
    cErrorMismatch    = 3u, // used in readAcopy until and including this one
    cErased           = 4u,
    cTransferError    = 5u
  };

  enum class Task : uint8_t {
    cCopy    = 0u,
    cCheck   = 1u,
    cCheckFf = 2u
  };

  enum class Modified : uint8_t {
    cNo    = 0u,
    cYes   = 1u,
    cYesFf = 2u
  };
  
  static uint32_t          sStartPage;
  static ConfigItem*       sCache;                // index is id
  static bool*             sDirtyPages;           // index relative to copy start
  static uint8_t*          sReadAheadBuffer;
  static uint32_t          sFirstUsablePage;      // the first usable (at least partially free) page, relative to copy start
  static uint16_t          sFirstUsableByteIndex; // the first free byte in the first usable page
  static uint16_t          sNextId;               // the next id to use when adding a new item

  FlashConfig() = delete;

  static constexpr uint32_t getPagesNeeded() noexcept {
    return tPagesNeeded;
  }

  static void init(uint32_t const aStartPage) {
    sStartPage = aStartPage;
    sCache = tInterface::template _newArray<ConfigItem>(tMaxItemCount);
    sDirtyPages = tInterface::template _newArray<bool>(cCopySizeInPages);
    sReadAheadBuffer = tInterface::template _newArray<uint8_t>(tReadAheadSizeInPages * cPageSizeInBytes);
    readAll();
  }

  static void done() {
    tInterface::template _deleteArray<ConfigItem>(sCache);
    tInterface::template _deleteArray<bool>(sDirtyPages);
    tInterface::template _deleteArray<uint8_t>(sReadAheadBuffer);
  }

public:
  static uint8_t const * getConfig(uint16_t const aId) {
    uint8_t const * result = nullptr;
    if(aId >= sNextId) {
      tInterface::fatalError(FlashException::cConfigInvalidId);
    }
    else {
      result = sCache[aId].getData();
    }
    return result;
  }

  static uint16_t addConfig(uint8_t const * const aData, uint16_t const aCount);
  static void setConfig(uint16_t const aId, uint8_t const * const aData);

  static void makeAllDirty() noexcept {
    std::fill_n(sDirtyPages, cCopySizeInPages, true);
  }

  static void commit() {
    bool ok = commit(0u);
    if(ok && tCopies == FlashCopies::c2) {
      ok = commit(cCopySizeInPages);
    }
    else { // nothing to do
    }
    if(ok) {
      makeAllClean();
    }
    else {
      tInterface::fatalError(FlashException::cFlashTransferError);
    }
  }

  static void clear() noexcept {
    sNextId = 0u; // do not wipe cache, as its lengths are already correct, and no need to repeat allocation
    makeAllClean();
  }

  // TODO remove
  static void readAllDebugTodoRemove() {
    readAll();
  }

private:
  static void makeAllClean() noexcept {
    std::fill_n(sDirtyPages, cCopySizeInPages, false);
  }

  static void readAll();
  static ReadResult readAcopy(uint32_t const aCopyOffsetInPages, Task const aTask);
  static ReadResult processPage(uint8_t const * const aPage, uint32_t const aPageIndexRelCopy, Task const aTask) noexcept;
  static bool commit(uint32_t const aCopyOffsetInPages) noexcept;
  static void serialize(uint32_t const aReadAheadStartPage, uint32_t const aPageInReadAhead) noexcept;
};

template<typename tInterface, uint32_t tPagesNeeded, FlashCopies tCopies, uint32_t tReadAheadSizeInPages, uint32_t tMaxItemCount, uint32_t tValueBufferSize>
uint16_t FlashConfig<tInterface, tPagesNeeded, tCopies, tReadAheadSizeInPages, tMaxItemCount, tValueBufferSize>::addConfig(uint8_t const * const aData, uint16_t const aCount) {
  uint16_t id = cUnusedValue;
  if(aCount > cMaxItemDataSize) {
    tInterface::fatalError(FlashException::cConfigItemTooBig);
  }
  else if(sNextId >= cUnusedValue) {
    tInterface::fatalError(FlashException::cConfigInvalidId);
  }
  else {
    id = sNextId++;
    uint16_t totalLeftover = cPageSizeInBytes - sFirstUsableByteIndex;
    if(totalLeftover < aCount) {
      ++sFirstUsablePage;
      sFirstUsableByteIndex = cOffsetPageItems;
    }
    else { // nothing to do
    }
    if(sFirstUsablePage < cCopySizeInPages) {
      ConfigItem& item = sCache[id];
      item.init(sFirstUsablePage, sFirstUsableByteIndex + cOffsetItemData, aCount);
      sFirstUsableByteIndex += cOffsetItemData + aCount;
      sDirtyPages[item.getPageIndex()] = true;
      item.setData(aData);
    }
    else {
      tInterface::fatalError(FlashException::cConfigFull);
    }
  }
  return id;
}
  
template<typename tInterface, uint32_t tPagesNeeded, FlashCopies tCopies, uint32_t tReadAheadSizeInPages, uint32_t tMaxItemCount, uint32_t tValueBufferSize>
void FlashConfig<tInterface, tPagesNeeded, tCopies, tReadAheadSizeInPages, tMaxItemCount, tValueBufferSize>::setConfig(uint16_t const aId, uint8_t const * const aData) {
  if(aId >= sNextId) {
    tInterface::fatalError(FlashException::cConfigInvalidId);
  }
  else {
    ConfigItem& item = sCache[aId];
    if(!item.doesMatch(aData)) {  
      sDirtyPages[item.getPageIndex()] = true;
      item.setData(aData);
    }
    else { // nothing to do
    }
  }
}

template<typename tInterface, uint32_t tPagesNeeded, FlashCopies tCopies, uint32_t tReadAheadSizeInPages, uint32_t tMaxItemCount, uint32_t tValueBufferSize>
void FlashConfig<tInterface, tPagesNeeded, tCopies, tReadAheadSizeInPages, tMaxItemCount, tValueBufferSize>::readAll() {
  clear();
  ReadResult result1 = readAcopy(0u, Task::cCopy);
  uint32_t firstUsablePage1 = sFirstUsablePage;
  uint16_t firstUsableByteIndex1 = sFirstUsableByteIndex;
  if(tCopies == FlashCopies::c2) {
    ReadResult result2;
    if(result1 != ReadResult::cOk) {
      clear();
      result2 = readAcopy(cCopySizeInPages, Task::cCopy);
    }
    else { // nothing to do
      result2 = readAcopy(cCopySizeInPages, Task::cCheck);
    }

    if(result1 == ReadResult::cOk && result2 == ReadResult::cErrorMismatch) {
      sFirstUsablePage = 0u;
      sFirstUsableByteIndex = cOffsetPageItems;
      clear();
      tInterface::fatalError(FlashException::cConfigCopiesMismatch);
    }
    else if(result1 == ReadResult::cOk && result2 != ReadResult::cOk) {
      sFirstUsablePage = firstUsablePage1;
      sFirstUsableByteIndex = firstUsableByteIndex1;
      tInterface::fatalError(FlashException::cConfigBadCopy2);
    }
    else if(result1 != ReadResult::cOk && result2 == ReadResult::cOk) {
      tInterface::fatalError(FlashException::cConfigBadCopy1);
    }
    else if(result1 != ReadResult::cOk && result2 != ReadResult::cOk) {
      sFirstUsablePage = 0u;
      sFirstUsableByteIndex = cOffsetPageItems;
      clear();
      tInterface::fatalError(FlashException::cConfigBadCopies);
    }
    else { // nothing to do
    }
  }
  else {
    if(result1 != ReadResult::cOk) {
      sFirstUsablePage = 0u;
      sFirstUsableByteIndex = cOffsetPageItems;
      clear();
      tInterface::fatalError(FlashException::cConfigBadCopies);
    }
    else { // nothing to do
    }
  }
}

template<typename tInterface, uint32_t tPagesNeeded, FlashCopies tCopies, uint32_t tReadAheadSizeInPages, uint32_t tMaxItemCount, uint32_t tValueBufferSize>
typename FlashConfig<tInterface, tPagesNeeded, tCopies, tReadAheadSizeInPages, tMaxItemCount, tValueBufferSize>::ReadResult FlashConfig<tInterface, tPagesNeeded, tCopies, tReadAheadSizeInPages, tMaxItemCount, tValueBufferSize>::readAcopy(uint32_t const aCopyOffsetInPages, Task const aTask) {
  uint32_t pagesRead = 0u;
  uint32_t pagesLeftInBuffer = 0u;
  uint32_t pageIndex;
  sFirstUsablePage = 0u;
  sFirstUsableByteIndex = cOffsetPageItems;
  ReadResult result = ReadResult::cOk; 
  while(result == ReadResult::cOk) {
    if(pagesLeftInBuffer == 0u) {
      pagesLeftInBuffer = std::min(tReadAheadSizeInPages, cCopySizeInPages - pagesRead);
      if(tInterface::readPages(sStartPage + aCopyOffsetInPages + pagesRead, pagesLeftInBuffer, sReadAheadBuffer) != SpiResult::cOk) {
        result = ReadResult::cTransferError;
        break;
      }
      else { // nothing to do
      }
      pagesRead += pagesLeftInBuffer;
      pageIndex = 0u;
    }
    else { // nothing to do
    }
    if(pagesLeftInBuffer > 0u) {
      while(result == ReadResult::cOk && pagesLeftInBuffer > 0u) {
        ReadResult tmp = processPage(sReadAheadBuffer + pageIndex * cPageSizeInBytes, pagesRead + pageIndex, aTask);
        result = (result == ReadResult::cOk ? tmp : result);
        --pagesLeftInBuffer;
        ++pageIndex;
      }
    }
    else {
      break;
    }
  }
  return result == ReadResult::cErased ? ReadResult::cOk : result;
}

template<typename tInterface, uint32_t tPagesNeeded, FlashCopies tCopies, uint32_t tReadAheadSizeInPages, uint32_t tMaxItemCount, uint32_t tValueBufferSize>
typename FlashConfig<tInterface, tPagesNeeded, tCopies, tReadAheadSizeInPages, tMaxItemCount, tValueBufferSize>::ReadResult FlashConfig<tInterface, tPagesNeeded, tCopies, tReadAheadSizeInPages, tMaxItemCount, tValueBufferSize>::processPage(uint8_t const * const aPage, uint32_t const aPageIndexRelCopy, Task const aTask) noexcept {
  ReadResult result = ReadResult::cOk;
  if(is<Magic::cErased>(aPage[cOffsetPageMagic])) {
    if(aTask == Task::cCheckFf) {
      result = std::all_of(aPage, aPage + cPageSizeInBytes, [](uint8_t const aValue) {
        return aValue == static_cast<uint8_t>(Magic::cErased); } ) ? ReadResult::cErased : ReadResult::cErrorConsistency;
    }
    else {
      result = ReadResult::cErased;
    }
  }
  else if(is<Magic::cConfig>(aPage[cOffsetPageMagic])) {
    uint16_t newItemStart = cOffsetPageItems;
    uint16_t itemCount = getValue<uint16_t>(aPage + cOffsetPageCount);
    if(calculateChecksum(aPage) != getValue<uint16_t>(aPage + cOffsetPageChecksum)) {
      result = ReadResult::cErrorChecksum;
    }
    else if(itemCount == 0u || itemCount == cUnusedValue) {
      result = ReadResult::cErrorConsistency;
    }
    else {
      sFirstUsablePage = (aTask == Task::cCheckFf ? sFirstUsablePage : aPageIndexRelCopy);
      while(itemCount > 0u) {
        uint8_t const * rawItemPointer = aPage + newItemStart;
        uint16_t id = getValue<uint16_t>(rawItemPointer + cOffsetItemId);
        uint16_t count = getValue<uint16_t>(rawItemPointer + cOffsetItemCount);
        newItemStart += cOffsetItemData;
        rawItemPointer = aPage + newItemStart;
        if(newItemStart + count > cPageSizeInBytes || id > sNextId) {
          result = ReadResult::cErrorConsistency;
          break;
        }
        else { // nothing to do
        }
        if(id == sNextId && aTask == Task::cCopy) {
          sCache[id].init(aPageIndexRelCopy, newItemStart, count);
          ++sNextId;
        }
        else if(id > sNextId || sCache[id].getCount() != count ) {
          result = ReadResult::cErrorConsistency;
        }
        else { // nothing to do
        }
        ConfigItem& item = sCache[id];
        if(aTask == Task::cCopy) {
          item.setData(rawItemPointer);
        }
        else {
          result = (item.doesMatch(rawItemPointer) ? ReadResult::cOk : ReadResult::cErrorMismatch);
        }
        newItemStart += count;
        sFirstUsableByteIndex = (aTask == Task::cCheckFf ? sFirstUsableByteIndex : newItemStart);
        --itemCount;
      }     
    }
  }
  else {
    result = ReadResult::cErrorConsistency;
  }
  return result;
}

template<typename tInterface, uint32_t tPagesNeeded, FlashCopies tCopies, uint32_t tReadAheadSizeInPages, uint32_t tMaxItemCount, uint32_t tValueBufferSize>
bool FlashConfig<tInterface, tPagesNeeded, tCopies, tReadAheadSizeInPages, tMaxItemCount, tValueBufferSize>::commit(uint32_t const aCopyOffsetInPages) noexcept {
  uint32_t const copySizeInPages = cCopySizeInPages;
  uint32_t const globalEndPageIndex = std::min(copySizeInPages, sFirstUsablePage + 1u);
  auto dirtyEnd = sDirtyPages + std::min(copySizeInPages, sFirstUsablePage + 1u);
  auto dirtyIter = std::find(sDirtyPages, dirtyEnd, true);
  bool ok = true;
  while(ok && dirtyIter != dirtyEnd) {
    uint32_t const startSector = (dirtyIter - sDirtyPages) / cSectorSizeInPages;
    uint32_t const startPage = startSector * cSectorSizeInPages;
    uint32_t const endPage = std::min<uint32_t>(globalEndPageIndex, startPage + tReadAheadSizeInPages);
    uint32_t const pageCount = endPage - startPage;
    uint32_t const sectorCount = (pageCount + cSectorSizeInPages - 1u) / cSectorSizeInPages;
    dirtyIter = std::find(sDirtyPages + endPage, dirtyEnd, true);
    if(tInterface::readPages(sStartPage + aCopyOffsetInPages + startPage, pageCount, sReadAheadBuffer) != SpiResult::cOk) {
      ok = false;
      break;
    }
    else { // nothing to do
    }
    for(uint32_t sectorIndex = 0; ok && sectorIndex < sectorCount; ++sectorIndex) {
      bool allErased = true;
      bool somethingChanged = false;  
      for(uint32_t pageIndex = 0; pageIndex < cSectorSizeInPages; ++pageIndex) {
        uint32_t pageInReadAhead = pageIndex + sectorIndex * cSectorSizeInPages;
        ReadResult result = processPage(sReadAheadBuffer + pageInReadAhead * cPageSizeInBytes, startPage + pageInReadAhead, Task::cCheckFf);
        somethingChanged = (somethingChanged || result != ReadResult::cOk);
        allErased =        (allErased        && result == ReadResult::cErased);
      }
      if(somethingChanged) {
        if(allErased) {
          for(uint32_t pageIndex = 0; ok && pageIndex < cSectorSizeInPages; ++pageIndex) {
            uint32_t pageInReadAhead = pageIndex + sectorIndex * cSectorSizeInPages;
            if(sDirtyPages[startPage + pageInReadAhead]) {
              serialize(startPage, pageInReadAhead);
              if(tInterface::writePage(sStartPage + aCopyOffsetInPages + startPage + pageInReadAhead, sReadAheadBuffer + pageInReadAhead * cPageSizeInBytes) != SpiResult::cOk) {
                ok = false;
              }
              else { // nothing to do
              }
            }
            else { // nothing to do
            }
          }
        }
        else {
          if(tInterface::eraseSector((sStartPage + aCopyOffsetInPages) / cSectorSizeInPages + startSector + sectorIndex) != SpiResult::cOk) {
            ok = false;
          }
          else { // nothing to do
          }
          for(uint32_t pageIndex = 0; ok && pageIndex < cSectorSizeInPages; ++pageIndex) {
            uint32_t pageInReadAhead = pageIndex + sectorIndex * cSectorSizeInPages;
            if(startPage + pageInReadAhead < sFirstUsablePage || 
              (startPage + pageInReadAhead == sFirstUsablePage && sFirstUsableByteIndex > cOffsetPageItems)) {
              serialize(startPage, pageInReadAhead);
              if(tInterface::writePage(sStartPage + aCopyOffsetInPages + startPage + pageInReadAhead, sReadAheadBuffer + pageInReadAhead * cPageSizeInBytes) != SpiResult::cOk) {
                ok = false;
              }
              else { // nothing to do
              }
            }
            else { // nothing to do
            }
          }
        }
      }
      else { // nothing to do
      }
    }
  }
  return ok;
}

template<typename tInterface, uint32_t tPagesNeeded, FlashCopies tCopies, uint32_t tReadAheadSizeInPages, uint32_t tMaxItemCount, uint32_t tValueBufferSize>
void FlashConfig<tInterface, tPagesNeeded, tCopies, tReadAheadSizeInPages, tMaxItemCount, tValueBufferSize>::serialize(uint32_t const aReadAheadStartPage, uint32_t const aPageInReadAhead) noexcept {
  uint8_t* page = sReadAheadBuffer + aPageInReadAhead * cPageSizeInBytes;
  uint16_t pageIndex = aReadAheadStartPage + aPageInReadAhead;
  uint16_t id = std::lower_bound(sCache, sCache + tMaxItemCount, pageIndex, [pageIndex](ConfigItem const &aItem, uint32_t const aIndex){
    return aItem.getPageIndex() < aIndex;
  }) - sCache;
  page[cOffsetPageMagic] = static_cast<uint8_t>(Magic::cConfig);
  int16_t count = 0u;
  int16_t newItemStart = cOffsetPageItems;
  while(id < sNextId && newItemStart < cPageSizeInBytes - cOffsetItemData - sCache[id].getCount()) {
    ConfigItem& item = sCache[id];
    setValue<uint16_t>(page + newItemStart + cOffsetItemId, id);
    setValue<uint16_t>(page + newItemStart + cOffsetItemCount, item.getCount());
    newItemStart += cOffsetItemData;
    std::copy_n(const_cast<uint8_t*>(item.getData()), item.getCount(), page + newItemStart);
    newItemStart += item.getCount();
    ++count;
    ++id;
  }
  // leave rubbish in the unused bytes, and calculate it into the checksum
  setValue<uint16_t>(page + cOffsetPageCount, count);
  setValue<uint16_t>(page + cOffsetPageChecksum, calculateChecksum(page));
}

template<typename tInterface, uint32_t tPagesNeeded, FlashCopies tCopies, uint32_t tReadAheadSizeInPages, uint32_t tMaxItemCount, uint32_t tValueBufferSize>
uint32_t FlashConfig<tInterface, tPagesNeeded, tCopies, tReadAheadSizeInPages, tMaxItemCount, tValueBufferSize>::sStartPage;

template<typename tInterface, uint32_t tPagesNeeded, FlashCopies tCopies, uint32_t tReadAheadSizeInPages, uint32_t tMaxItemCount, uint32_t tValueBufferSize>
typename FlashConfig<tInterface, tPagesNeeded, tCopies, tReadAheadSizeInPages, tMaxItemCount, tValueBufferSize>::ConfigItem* FlashConfig<tInterface, tPagesNeeded, tCopies, tReadAheadSizeInPages, tMaxItemCount, tValueBufferSize>::sCache;

template<typename tInterface, uint32_t tPagesNeeded, FlashCopies tCopies, uint32_t tReadAheadSizeInPages, uint32_t tMaxItemCount, uint32_t tValueBufferSize>
bool* FlashConfig<tInterface, tPagesNeeded, tCopies, tReadAheadSizeInPages, tMaxItemCount, tValueBufferSize>::sDirtyPages;

template<typename tInterface, uint32_t tPagesNeeded, FlashCopies tCopies, uint32_t tReadAheadSizeInPages, uint32_t tMaxItemCount, uint32_t tValueBufferSize>
uint8_t* FlashConfig<tInterface, tPagesNeeded, tCopies, tReadAheadSizeInPages, tMaxItemCount, tValueBufferSize>::sReadAheadBuffer;

template<typename tInterface, uint32_t tPagesNeeded, FlashCopies tCopies, uint32_t tReadAheadSizeInPages, uint32_t tMaxItemCount, uint32_t tValueBufferSize>
uint32_t FlashConfig<tInterface, tPagesNeeded, tCopies, tReadAheadSizeInPages, tMaxItemCount, tValueBufferSize>::sFirstUsablePage;

template<typename tInterface, uint32_t tPagesNeeded, FlashCopies tCopies, uint32_t tReadAheadSizeInPages, uint32_t tMaxItemCount, uint32_t tValueBufferSize>
uint16_t FlashConfig<tInterface, tPagesNeeded, tCopies, tReadAheadSizeInPages, tMaxItemCount, tValueBufferSize>::sFirstUsableByteIndex;

template<typename tInterface, uint32_t tPagesNeeded, FlashCopies tCopies, uint32_t tReadAheadSizeInPages, uint32_t tMaxItemCount, uint32_t tValueBufferSize>
uint16_t FlashConfig<tInterface, tPagesNeeded, tCopies, tReadAheadSizeInPages, tMaxItemCount, tValueBufferSize>::sNextId;

}
#endif
