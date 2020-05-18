#ifndef NOWTECH_FLASHLOADBALANCING
#define NOWTECH_FLASHLOADBALANCING

#include "FlashCommon.h"

namespace nowtech::memory {

template<typename tInterface, uint32_t tPagesNeeded, uint16_t tBalancingInitialFillCount, uint32_t tLeoMaxCount, uint32_t tReadAheadSizeInPages>
class FlashLoadBalancing final : public FlashCommon<tInterface> {
  static_assert(tReadAheadSizeInPages > 1u);

  template<typename tInterfaceOther, typename tPlugin1, typename tPlugin2, typename tPlugin3>
  friend class FlashPartitioner;
  
  using FlashCommon<tInterface>::cPageSizeInBytes;
  using FlashCommon<tInterface>::cSectorSizeInPages;
  using FlashCommon<tInterface>::cFlashSizeInPages;
  using FlashCommon<tInterface>::cOffsetPageMagic;
  using FlashCommon<tInterface>::cOffsetPageCount;
  using FlashCommon<tInterface>::cOffsetPageChecksum;
  using FlashCommon<tInterface>::cUnusedValue;
  using FlashCommon<tInterface>::calculateChecksum;

private:
  static uint32_t            sStartPage;
  static uint8_t*          sReadAheadBuffer;

  FlashLoadBalancing() = delete;

  static constexpr uint32_t getPagesNeeded() noexcept {
    return tPagesNeeded;
  }

  static void init(uint32_t const aStartPage) noexcept {
    sStartPage = aStartPage;
    sReadAheadBuffer = tInterface::_newArray<uint8_t>(tReadAheadSizeInPages * tInterface::getPageSizeInBytes());
  }

  static void done() noexcept {
    tInterface::_deleteArray<uint8_t>(tReadAheadBuffer);
  }
};

template<typename tInterface, uint32_t tPagesNeeded, uint16_t tBalancingInitialFillCount, uint32_t tLeoMaxCount, uint32_t tReadAheadSizeInPages>
uint32_t FlashLoadBalancing<tInterface, tPagesNeeded, tBalancingInitialFillCount, tLeoMaxCount, tReadAheadSizeInPages>::sStartPage;

template<typename tInterface, uint32_t tPagesNeeded, uint16_t tBalancingInitialFillCount, uint32_t tLeoMaxCount, uint32_t tReadAheadSizeInPages>
uint8_t FlashLoadBalancing<tInterface, tPagesNeeded, tBalancingInitialFillCount, tLeoMaxCount, tReadAheadSizeInPages>::sReadAheadBuffer;

}

#endif
