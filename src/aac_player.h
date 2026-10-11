#pragma once

#include <cstddef>
#include <cstdint>

// ADTS AAC(-LC)のバイト列をデコードし、I2S(ES8388)へ再生する。
// このアプリではインスタンスは常に1つだけなので、状態はモジュール内部に隠蔽する。

// falseなら初期化失敗。デコーダ状態(約29KB、SBR有効時は約80KB)は codec_arena.h の
// 静的領域に置くのでヒープは使わないが、Helix のフレーム入力(2KB)と PCM 出力(8KB)の
// バッファはヒープから取られる。初回はWi-Fi接続より前に呼ぶこと。
bool aacPlayerBegin();

// デコーダを閉じて codec_arena を明け渡す(Opus へ切り替える前に必ず呼ぶ)。Helix の
// フレーム/PCM バッファ(ヒープ)もここで解放される。次の aacPlayerBegin() で作り直す。
void aacPlayerEnd();

// dataはADTSフレームが連続したバイト列(ID3v2ヘッダは事前に取り除いておくこと)。
// 任意サイズで呼んでよい(内部でフレーム境界をまたいでバッファリングされる)。
void aacPlayerFeed(const uint8_t* data, size_t len);

// aacPlayerBegin()以降にデコードできたフレーム数。チャンネル切り替えのたびに
// aacPlayerBegin()が呼ばれるのでゼロに戻る。「セグメントは流れているのに
// デコーダがフレームを1つも出していない」状態を切り分けるための診断用。
uint32_t aacPlayerFrameCount();
