#pragma once

#include <cstddef>
#include <cstdint>

#include "config.h"

// 音声デコーダ(AAC / Opus)の状態を置く静的アリーナ。再生中に使うデコーダは
// 常に1つなので、両者で同じ領域を排他的に使う(サイズは config::kCodecArenaBytes、
// 実測値と根拠はそちらのコメントを参照)。
//
// 使う側の約束:
//   - begin() で必要サイズを codecArenaBytes() と比較し、足りなければ失敗する。
//   - 別のデコーダへ切り替える前に、前のデコーダの end() を済ませておく
//     (free は不要。領域の再利用は次の begin() の初期化で行う)。
uint8_t* codecArena();
constexpr size_t codecArenaBytes() { return config::kCodecArenaBytes; }
