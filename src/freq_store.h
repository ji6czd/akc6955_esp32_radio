#pragma once

#include <cstdint>

// DSPラジオ(AKC6955)のバンドごとの最後の周波数の不揮発保存(Preferences/NVS)を担当する。
// volume_store.hと同じく、変更の都度は書き込まず、一定時間(config::kFreqSaveDebounceMs)
// 選局が無かった場合にのみ実際に書き込む(デバウンス)。

// バンド番号の上限(保存スロット数)。
constexpr int kFreqStoreMaxBands = 8;

// 保存済みの周波数(kHz)を返す。無ければdefaultKhzを返す。
// まだNVSへ書き込まれていない変更があれば、それを返す。
uint32_t freqStoreLoad(int band, uint32_t defaultKhz);

// 選局したら(または聴き終わるとき)呼ぶ。この時点では書き込まない。
// 実際の書き込みはfreqStoreTick()側で行う。
void freqStoreNotifyChanged(int band, uint32_t khz);

// loop()から毎回呼ぶ。デバウンス期間経過後、変更が溜まっていれば実際にPreferencesへ書き込む。
void freqStoreTick();
