#include "FlashPartitioner.h"
#include "FlashConfig.h"
//#include "FlashLongtermBulk.h"
//#include "FlashLoadBalancing.h"
#include "FibonacciMemoryManager.h"
#include <iostream>
#include <iomanip>
#include <numeric>
#include <algorithm>

class FibonacciInterface final {
public:
  static void badAlloc() {
    std::cout << "FibonacciInterface::badAlloc()\n";
  }

  static void lock() {
  }

  static void unlock() {
  }
};

constexpr uint32_t cMemorySize          =  1024u * 128u;
constexpr uint32_t cMinBlockSize        =    32u;
constexpr uint32_t cUserAlign           =     8u;
constexpr uint32_t cFibonacciDifference =     3u;

typedef nowtech::memory::NewDelete<FibonacciInterface, cMemorySize, cMinBlockSize, cUserAlign, cFibonacciDifference> FlashNewDelete;

class FlashInterface final {
private:
  static constexpr uint32_t cPageSizeInBytes     =   256u;
  static constexpr uint32_t cSectorSizeInPages   =    16u;
  static constexpr uint32_t cFlashSizeInPages    = 65536u;
  static constexpr uint32_t cSectorSizeInBytes   = cPageSizeInBytes * cSectorSizeInPages;
  static constexpr uint32_t cFlashSizeInSectors  = cFlashSizeInPages / cSectorSizeInPages;
  static constexpr uint32_t cErasedByte          =   255u;
  static constexpr uint32_t cPatternSize         = cPageSizeInBytes;

  static constexpr char cExceptionTexts[][22] = { "cCommunication", "cConfigBadCopy1", "cConfigBadCopy2", "cConfigBadCopies", "cConfigCopiesMismatch", "cConfigInvalidId", "cConfigFull", "cConfigItemTooBig", "cConfigCommitError" };

  static uint8_t* sMemoryFlash;
  static uint8_t* sMemoryRam;
  static bool     sMapped;

public:
  static uint8_t* sPattern;

  static void init() {
    sMapped = false;
    sMemoryFlash = new uint8_t[cPageSizeInBytes * cFlashSizeInPages];
    sMemoryRam = new uint8_t[cMemorySize];
    sPattern = new uint8_t[cPatternSize];
    std::iota(sPattern, sPattern + cPatternSize, 0u);
    FlashNewDelete::init(sMemoryRam, false);
  }

  static void done() {
    delete[] sPattern;
    delete[] sMemoryFlash;
    delete[] sMemoryRam;
  }

  static constexpr uint32_t getPageSizeInBytes() noexcept {
    return cPageSizeInBytes;
  }

  static constexpr uint32_t getSectorSizeInPages() noexcept {
    return cSectorSizeInPages;
  }

  static constexpr uint32_t getFlashSizeInPages() noexcept {
    return cFlashSizeInPages;
  }

  static void badAlloc() {
    std::cout << "bad alloc\n";
  }

  static void fatalError(nowtech::memory::FlashException const aException) {
    std::cout << "fatal error: " << cExceptionTexts[static_cast<uint8_t>(aException)] << '\n';
  }

  template<typename tClass, typename ...tParameters>
  static tClass* _new(tParameters... aParameters) {
    return FlashNewDelete::template _new<tClass, tParameters...>(aParameters...);
  }

  template<typename tClass>
  static tClass* _newArray(uint32_t const aCount) {
    return FlashNewDelete::template _newArray<tClass>(aCount);
  }

  template<typename tClass>
  static void _delete(tClass* aPointer) {
    return FlashNewDelete::template _delete<tClass>(aPointer);
  }

  template<typename tClass>
  static void _deleteArray(tClass* aPointer) {
    return FlashNewDelete::template _deleteArray<tClass>(aPointer);
  }

  static bool canMapMemory() noexcept {
    return true;
  }

  static nowtech::memory::SpiResult setMappedMode(bool const aMapped) noexcept {
    sMapped = aMapped;
    return nowtech::memory::SpiResult::cOk;
  }

  static nowtech::memory::SpiResult readMapped(uint32_t const aAddress, uint8_t aCount, uint8_t * const aData) noexcept {
    nowtech::memory::SpiResult result;
    if(!sMapped) {
      result = nowtech::memory::SpiResult::cMap;
    }
    else if(aAddress < cFlashSizeInPages * cPageSizeInBytes && aAddress + aCount < cFlashSizeInPages * cPageSizeInBytes) {
      std::copy_n(sMemoryFlash + aAddress, aCount, aData);
      result = nowtech::memory::SpiResult::cOk;
    }
    else {
      std::cout << "readMapped: invalid address " << aAddress << " or count " << aCount << '\n';
      result = nowtech::memory::SpiResult::cInvalid;
    }
    return result;
  }

  static nowtech::memory::SpiResult findPageWithDesiredMagic(uint32_t const aStartPage, uint32_t const aEndPage, uint8_t const aDesiredMagic, uint32_t * const aResultStart, uint32_t * const aResultEnd) noexcept {
    nowtech::memory::SpiResult result;
    if(sMapped) {
      result = nowtech::memory::SpiResult::cMap;
    }
    else if(aStartPage < aEndPage && aEndPage < cFlashSizeInPages) {
      *aResultStart = *aResultEnd = aEndPage; // TODO this is wrong, implement if needed.
      result = nowtech::memory::SpiResult::cOk; // or cMissing if not found
    }
    else {
      std::cout << "findPageWithDesiredMagic: invalid start page " << aStartPage << " or end page " << aEndPage << '\n';
      result = nowtech::memory::SpiResult::cInvalid;
    }
    return result;
  }

  static nowtech::memory::SpiResult eraseSector(uint32_t const aSector) noexcept {
    nowtech::memory::SpiResult result;
    if(sMapped) {
      result = nowtech::memory::SpiResult::cMap;
    }
    else if(aSector < cFlashSizeInSectors) {
      uint8_t const erasedByte = cErasedByte;
      std::fill_n(sMemoryFlash + aSector * cSectorSizeInBytes, cSectorSizeInBytes, erasedByte);
      std::cout << "erased sector: " << aSector << " (pages " << aSector * cSectorSizeInPages << " - " << (aSector + 1u) * cSectorSizeInPages - 1u << ")\n";
      result = nowtech::memory::SpiResult::cOk;
    }
    else {
      std::cout << "eraseSector: invalid sector " << aSector << '\n';
      result = nowtech::memory::SpiResult::cInvalid;
    }
    return result;
  }

  static nowtech::memory::SpiResult writePage(uint32_t const aPage, uint8_t const * const aData) noexcept {
    nowtech::memory::SpiResult result;
    if(sMapped) {
      result = nowtech::memory::SpiResult::cMap;
    }
    else if(aPage < cFlashSizeInPages) {
      std::copy_n(aData, cPageSizeInBytes, sMemoryFlash + aPage * cPageSizeInBytes);
      std::cout << "wrote page: " << aPage << '\n';
      for(uint16_t i = 0u; i < 16u; ++i) {
        for(uint16_t j = 0u; j < 16u; ++j) {
          std::cout << std::setw(2) << std::setfill('0') << std::hex << static_cast<uint16_t>(sMemoryFlash[aPage * cPageSizeInBytes + i * 16u + j]) << ' ';
        }
        std::cout << '\n';
      }
      std::cout << '\n' << std::dec;
      result = nowtech::memory::SpiResult::cOk;
    }
    else {
      std::cout << "writePage: invalid page " << aPage << '\n';
      result = nowtech::memory::SpiResult::cInvalid;
    }
    return result;
  }

  static nowtech::memory::SpiResult readPages(uint32_t const aStartPage, uint32_t const aPageCount, uint8_t * const aData) noexcept {
    nowtech::memory::SpiResult result;
    if(sMapped) {
      result = nowtech::memory::SpiResult::cMap;
    }
    else if(aStartPage < cFlashSizeInPages && aStartPage + aPageCount <= cFlashSizeInPages) {
      std::copy_n(sMemoryFlash + aStartPage * cPageSizeInBytes, cPageSizeInBytes * aPageCount, aData);
/*      for(uint32_t k = 0; k < aPageCount; ++k) {
        std::cout << std::dec << "reading page: " << aStartPage + k << '\n';
        for(uint16_t i = 0u; i < 16u; ++i) {
          for(uint16_t j = 0u; j < 16u; ++j) {
            std::cout << std::setw(2) << std::setfill('0') << std::hex << static_cast<uint16_t>(sMemoryFlash[(aStartPage + k) * cPageSizeInBytes + i * 16u + j]) << ' ';
          }
          std::cout << '\n';
        }
      }
      std::cout << '\n' << std::dec;*/
      std::cout << "read pages " << aStartPage << " - " << aStartPage + aPageCount - 1u << "\n";
      result = nowtech::memory::SpiResult::cOk;
    }
    else {
      std::cout << "readPages: invalid pages: " << aStartPage << " count: " << aPageCount << '\n';
      result = nowtech::memory::SpiResult::cInvalid;
    }
    return result;
  }
};
  
constexpr char FlashInterface::cExceptionTexts[][22];
uint8_t* FlashInterface::sMemoryFlash;
uint8_t* FlashInterface::sMemoryRam;
uint8_t* FlashInterface::sPattern;
bool     FlashInterface::sMapped;

constexpr nowtech::memory::FlashCopies cCopies               = nowtech::memory::FlashCopies::c2;
constexpr uint32_t                     cPagesNeeded          = 4096u;
constexpr uint32_t                     cReadAheadSizeInPages =   48u;
constexpr uint32_t                     cMaxItemCount         =   20u;
constexpr uint32_t                     cValueBufferSize      =    8u;

typedef nowtech::memory::FlashConfig<FlashInterface, cPagesNeeded, cCopies, cReadAheadSizeInPages, cMaxItemCount, cValueBufferSize>   DebugFlashConfig;
typedef nowtech::memory::FlashPartitioner<FlashInterface, DebugFlashConfig, nowtech::memory::NullPlugin, nowtech::memory::NullPlugin> DebugFlashPartitioner;

void testConfig1() {
  uint16_t lastId;
  for(uint16_t i = 1u; i < 80u; i += 5u) {
    lastId = DebugFlashConfig::addConfig(FlashInterface::sPattern, i);
  }
  DebugFlashConfig::commit();
  DebugFlashConfig::clear();
  std::cout << " --- clr --- \n";
  DebugFlashConfig::readAllDebugTodoRemove();
  for(uint16_t i = 0u; i < lastId; ++i) {
    uint8_t const * data = DebugFlashConfig::getConfig(i);
    std::cout << i << '\n';
    for(uint16_t j = 0u; j < 1u + i * 5u; ++j) {
      std::cout << static_cast<uint16_t>(data[j]) << ' ';
    }
    std::cout << "\n\n";
  }
}

int main() {
  FlashInterface::init();
  DebugFlashPartitioner::init();
  testConfig1(); 
  DebugFlashPartitioner::done();
  FlashInterface::done();
}
