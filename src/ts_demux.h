#pragma once

#include <cstddef>
#include <cstdint>

// MPEG-TS(188バイト/パケット)から音声ES(ADTSフレーム列)だけを抜き出す。
// PAT→PMTを辿って音声PID(stream_type 0x0F=ADTS AAC, 0x11=LOAS/LATM AAC)を特定し、
// そのPIDのPESペイロードからPESヘッダを剥がしてES本体だけをコールバックへ渡す。
// 映像PID・ID3タイムドメタデータPID等はPMTに現れた時点で無視され、読み捨てられる。
//
// ListenRadio(smartstream.ne.jp)のHLSセグメントは生MPEG-TSで配信されており、
// radiko/NHKのような素のADTSチャンク直送ではないために必要になった。
//
// PAT/PMTはセグメント(HTTPレスポンス)冒頭で毎回送出される想定なので、セグメントごとに
// 新しいインスタンスを使うこと(AesCbcStreamDecryptorと同じ寿命)。
class TsDemuxer {
 public:
  // 抜き出したES(ADTSフレームが連続したバイト列)を渡すコールバック。
  // dataはfeed()呼び出し中だけ有効(コピーするかその場で消費すること)。
  using AudioCallback = void (*)(const uint8_t* data, size_t len);

  explicit TsDemuxer(AudioCallback onAudio) : onAudio_(onAudio) {}

  // 任意サイズのバイト列を渡す。188バイト境界をまたぐ端数は次回のfeed()へ持ち越す。
  void feed(const uint8_t* data, size_t len);

 private:
  void handlePacket(const uint8_t* pkt);
  void parsePat(const uint8_t* payload, size_t len, bool pusi);
  void parsePmt(const uint8_t* payload, size_t len, bool pusi);
  void parseAudioPes(const uint8_t* payload, size_t len, bool pusi);

  AudioCallback onAudio_;
  int pmtPid_ = -1;
  int audioPid_ = -1;

  static constexpr size_t kPacketSize = 188;
  uint8_t carry_[kPacketSize];
  size_t carryLen_ = 0;
};
