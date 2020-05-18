#ifndef NOWTECH_FLASHPARTITIONER
#define NOWTECH_FLASHPARTITIONER

#include <cstddef>
#include <cstdint>

namespace nowtech::memory {

class NullPlugin final {
  template<typename tInterface, typename tPlugin1, typename tPlugin2, typename tPlugin3>
  friend class FlashPartitioner;

private:
  NullPlugin() = delete;
  
  static constexpr uint32_t getPagesNeeded() noexcept {
    return 0u;
  }

  static void init(uint32_t const) noexcept {
  }

  static void done() noexcept {
  }
};

template<typename tInterface, typename tPlugin1, typename tPlugin2 = NullPlugin, typename tPlugin3 = NullPlugin>
class FlashPartitioner final {
private:
  static constexpr uint32_t countSetBits(uint32_t aNumber) noexcept { 
    uint32_t count = 0u;
    while (aNumber > 0u) { 
      aNumber &= (aNumber - 1u); 
      count++; 
    } 
    return count; 
  }
 
  static_assert(tInterface::getFlashSizeInPages() >= tPlugin1::getPagesNeeded() + tPlugin2::getPagesNeeded() + tPlugin3::getPagesNeeded(), "Flash partitions must fill the flash.");
  static_assert(static_cast<uint64_t>(tInterface::getFlashSizeInPages()) * static_cast<uint64_t>(tInterface::getPageSizeInBytes()) <= 1ull << 32ull, "Flash size must be less than 4G.");
  static_assert(countSetBits(tInterface::getFlashSizeInPages()) == 1u, "Flash size must be a power of 2 and positive.");
  static_assert(countSetBits(tInterface::getPageSizeInBytes()) == 1u, "Page size must be a power of 2 and positive.");
  static_assert(tInterface::getPageSizeInBytes() >=   256u, "Page size must be at least 256 bytes.");
  static_assert(tInterface::getPageSizeInBytes() <= 32768u, "Page size must be at most 32768 bytes.");
  static_assert(countSetBits(tInterface::getSectorSizeInPages()) == 1u, "Sector size must be a power of 2 and positive.");
  static_assert(tPlugin1::getPagesNeeded() % tInterface::getSectorSizeInPages() == 0u, "Partition sizes must be a multiply of sector size.");
  static_assert(tPlugin2::getPagesNeeded() % tInterface::getSectorSizeInPages() == 0u, "Partition sizes must be a multiply of sector size.");
  static_assert(tPlugin3::getPagesNeeded() % tInterface::getSectorSizeInPages() == 0u, "Partition sizes must be a multiply of sector size.");
  static_assert(tInterface::getFlashSizeInPages() > tInterface::getSectorSizeInPages(), "Flash Size must be grater than sector size.");

public:
  /// tInterface must be initialized on its own way beforehand
  static void init() {
    tPlugin1::init(0u);
    tPlugin2::init(tPlugin1::getPagesNeeded());
    tPlugin3::init(tPlugin1::getPagesNeeded() + tPlugin2::getPagesNeeded());
  }
  
  static void done() {
    tPlugin1::done();
    tPlugin2::done();
    tPlugin3::done();
  }
};

}

#endif
