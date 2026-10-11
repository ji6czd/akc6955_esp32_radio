#pragma once

#include <cstddef>
#include <cstdint>

// Opus パケット(JCBA サイマル: 48kHz ステレオ、20ms/パケット)をデコードして
// i2s_speaker へ流す。aac_player と同型で、インスタンスは常に1つ(モジュール内部に隠蔽)。
// デコーダ状態(stereo 26,520B、実測)は codec_arena.h の静的領域に置くので、AAC と
// 同時には使えない。切り替えるときは aacPlayerEnd() → opusPlayerBegin() の順に呼ぶこと
// (Helix 側を閉じずに Opus で領域を上書きすると、次の aacPlayerBegin() の flush() が
// 壊れた状態の上で走る)。デコード作業領域は C99 VLA でスタックに取られる(約10KB)ので、
// 呼び出すタスクのスタックはその分大きく取ること(config::kDecodeTaskStackBytes)。

// falseなら初期化失敗(アリーナ不足のみ。ヒープは使わない)。呼ぶたびに状態を作り直す。
bool opusPlayerBegin();

// アリーナを AAC に明け渡す前に呼ぶ(状態は持たないので実質フラグのリセットだけ)。
void opusPlayerEnd();

// Opus パケット1つ(Ogg のパケット境界どおり)をデコードして i2sSpeakerEnqueue() する。
// 呼び出し元のタスク以外から呼ばないこと(aacPlayerFeed と同じ不変条件)。
void opusPlayerFeedPacket(const uint8_t* data, size_t len);

// opusPlayerBegin() 以降にデコードできたパケット数(診断用。begin でゼロに戻る)。
uint32_t opusPlayerFrameCount();
