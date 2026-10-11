#include "channel_list.h"

#include "config.h"
#include "station_id_list.h"

// 空テーブルだと剰余演算でゼロ除算する。生成スクリプトが空を書き出すことは無いが、
// 差し替えミスをリンク前に落とすための保険。
static_assert(kStationCount > 0, "station_id_list.h has no stations");

// 起動時フォールバックのインデックス。保存値が使えないときは必ずここへ落ちるので、
// 範囲外だと getChannel() が nullptr を返し main.cpp の起動シーケンスが落ちる。
static_assert(config::kBootChannelIndex >= 0 && static_cast<size_t>(config::kBootChannelIndex) < kStationCount,
              "config::kBootChannelIndex is out of range for the station table");

namespace {

constexpr size_t pow10(unsigned n) { return n == 0 ? 1 : 10 * pow10(n - 1); }

// 直接選局コードは先頭1桁がプラットフォーム番号、残りが局番号。局番号の桁数で
// 表せる局数(10^桁 - 1)を超えると末尾の局に到達できなくなる。
constexpr size_t kStationNumberDigits = config::kDirectTuneDigits - 1;
constexpr size_t kMaxStationsPerPlatform = pow10(kStationNumberDigits) - 1;
constexpr int kTuneCodePlatformMultiplier = static_cast<int>(pow10(kStationNumberDigits));

constexpr bool platformCountsFit() {
  for (size_t p = 0; p < kChannelPlatformCount; ++p) {
    if (kPlatformRanges[p].count > kMaxStationsPerPlatform) {
      return false;
    }
  }
  return true;
}

// kPlatformRanges が kStations を列挙順に隙間なく覆い、各区間の行が本当にその
// プラットフォームであること(生成スクリプトの PLATFORM_ORDER と ChannelPlatform の
// ずれをコンパイル時に検出する)。
constexpr bool platformRangesConsistent() {
  size_t next = 0;
  for (size_t p = 0; p < kChannelPlatformCount; ++p) {
    if (kPlatformRanges[p].first != next) {
      return false;
    }
    for (size_t i = 0; i < kPlatformRanges[p].count; ++i) {
      if (static_cast<size_t>(kStations[next + i].platform) != p) {
        return false;
      }
    }
    next += kPlatformRanges[p].count;
  }
  return next == kStationCount;
}

const PlatformRange& rangeOf(size_t index) { return kPlatformRanges[static_cast<size_t>(kStations[index].platform)]; }

}  // namespace

static_assert(config::kDirectTuneDigits >= 2,
              "direct tune code needs a platform digit plus at least one station digit");
static_assert(platformCountsFit(), "a platform has more stations than kDirectTuneDigits-digit direct tune can address");
static_assert(platformRangesConsistent(), "kPlatformRanges does not match the layout of kStations");

size_t ChannelList::count() const { return kStationCount; }

const Channel* ChannelList::getChannel(size_t index) const {
  if (index >= kStationCount) {
    return nullptr;
  }
  return &kStations[index];
}

void ChannelList::setCurrentIndex(size_t index) {
  if (index < kStationCount) {
    currentIndex_.store(index, std::memory_order_relaxed);
  }
}

int ChannelList::indexForTuneCode(int code) const {
  if (code < 0) {
    return -1;
  }
  int platformNo = code / kTuneCodePlatformMultiplier;  // 1始まり
  int number = code % kTuneCodePlatformMultiplier;      // 1始まり
  if (platformNo < 1 || platformNo > static_cast<int>(kChannelPlatformCount)) {
    return -1;
  }
  const PlatformRange& r = kPlatformRanges[platformNo - 1];
  if (number < 1 || number > static_cast<int>(r.count)) {
    return -1;
  }
  return static_cast<int>(r.first) + number - 1;
}

int ChannelList::tuneCodeForIndex(size_t index) const {
  if (index >= kStationCount) {
    return 0;
  }
  const PlatformRange& r = rangeOf(index);
  int platformNo = static_cast<int>(kStations[index].platform) + 1;
  return platformNo * kTuneCodePlatformMultiplier + static_cast<int>(index - r.first) + 1;
}

// 以下、currentIndex_を複数回参照する関数は先頭で1回だけ読んでローカルに固定する。
// 別コアのsetCurrentIndex()が途中で走っても、関数内で見る値が食い違わないようにするため。

int ChannelList::nextAreaFirstIndex() const {
  const size_t cur = currentIndex_.load(std::memory_order_relaxed);
  const PlatformRange& r = rangeOf(cur);
  const size_t end = r.first + r.count;
  uint8_t currentArea = kStations[cur].area;

  // 現在のエリアの末尾を越えた最初の局。プラットフォームの末尾まで行ったら
  // そのプラットフォームの先頭へ回り込む。
  for (size_t i = cur + 1; i < end; ++i) {
    if (kStations[i].area != currentArea) {
      return static_cast<int>(i);
    }
  }
  return static_cast<int>(r.first);
}

int ChannelList::previousAreaFirstIndex() const {
  size_t i = currentIndex_.load(std::memory_order_relaxed);
  const PlatformRange& r = rangeOf(i);
  const size_t begin = r.first;
  uint8_t currentArea = kStations[i].area;

  // まず現在のエリアの先頭まで戻り、そこから更に1つ前のエリアの先頭を探す。
  // (エリアの途中にいるときは「1つ前のエリア」ではなく現在のエリアの先頭に
  //  戻ってほしいところだが、送り操作としては前エリアへ抜けるほうが素直なので
  //  常に前のエリアの先頭まで移動する。)
  while (i > begin && kStations[i - 1].area == currentArea) {
    --i;
  }
  if (i == begin) {
    // プラットフォーム先頭のエリアにいた場合は、同じプラットフォームの末尾エリアへ回り込む。
    i = begin + r.count;
  }
  uint8_t previousArea = kStations[i - 1].area;
  while (i > begin && kStations[i - 1].area == previousArea) {
    --i;
  }
  return static_cast<int>(i);
}

int ChannelList::nextPlatformFirstIndex() const {
  size_t p = static_cast<size_t>(kStations[currentIndex_.load(std::memory_order_relaxed)].platform);
  // 局が0のプラットフォームは飛ばす。kStationCount > 0 なので、一周すれば必ず自分に戻る。
  for (size_t n = 0; n < kChannelPlatformCount; ++n) {
    p = (p + 1) % kChannelPlatformCount;
    if (kPlatformRanges[p].count > 0) {
      break;
    }
  }
  return static_cast<int>(kPlatformRanges[p].first);
}

int ChannelList::previousPlatformFirstIndex() const {
  size_t p = static_cast<size_t>(kStations[currentIndex_.load(std::memory_order_relaxed)].platform);
  for (size_t n = 0; n < kChannelPlatformCount; ++n) {
    p = (p + kChannelPlatformCount - 1) % kChannelPlatformCount;
    if (kPlatformRanges[p].count > 0) {
      break;
    }
  }
  return static_cast<int>(kPlatformRanges[p].first);
}

const char* ChannelList::getCurrentAreaName() const {
  uint8_t area = kStations[currentIndex_.load(std::memory_order_relaxed)].area;
  if (area >= kAreaNameCount) {
    return "";
  }
  return kAreaNames[area];
}

uint64_t ChannelList::getFreeAreaMask(size_t index) const {
  if (index >= kStationCount) {
    return 0;
  }
  return kStationFreeAreaMask[index];
}

ChannelList g_channelList;
