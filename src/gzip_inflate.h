#pragma once

#include <cstddef>
#include <cstdint>

// gzip(RFC 1952)本文を展開する。ESP32 ROMにリンク済みのminiz(tinfl)を使うのでヒープも
// 追加フラッシュも消費しない。ListenRadio(smartstream.ne.jp)がAccept-Encodingの指定
// (identity)を無視して常にgzipでプレイリストを返してくることがあるため、透過的に
// 展開してから通常のHLSパーサへ渡う。
//
// inがgzipマジックバイト(1F 8B)で始まっていなければ何もせずfalseを返す(呼び出し側は
// 元のバイト列をそのまま使えばよい)。
// out/outCapacityは呼び出し側が確保する展開先バッファ。展開後のサイズがoutCapacityを
// 超える場合はfalse(呼び出し側でバッファを大きくすること)。
bool gzipInflateIfNeeded(const uint8_t* in, size_t inLen, uint8_t* out, size_t outCapacity, size_t* outLen);
