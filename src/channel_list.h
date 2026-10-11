#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

// 局の配信元。いずれもstreamIdにはURLそのものではなく「局を指す最小限のID」を
// 入れておき、再生開始時に実URLを組み立てる:
//   kNhk    … ストリーム番号(例: "3")。nhk_client.hのnhkBuildLiveUrl()で展開する。
//             NHKのURLは番号以外まったく同じ文字列なので、テーブルに持つ意味が無い。
//   kRadiko … 局ID(例: "TBS")。トークン・lsidが都度必要なのでradiko_client.hで組み立てる。
//   kJcba   … JCBAサイマルの局ID(例: "fmhana")。HLSではなく、
//             /api/select_stream?station=ID でトークンとWebSocket接続先をもらい、
//             Ogg/Opusをバイナリフレームで受ける方式(jcba_client.h / opus_player.h)。
//   kListenRadio … ListenRadioのChannelId(例: "30011")。HLSだが、セグメント本体が
//             ADTSの生バイト列ではなく生MPEG-TS(H.264映像+AAC-LC音声の多重)なので、
//             再生時にts_demux.hで音声ESだけを抜き出す。配信サーバがOriginヘッダを
//             CORSとして必須で見るため、master/media/セグメントすべてのHTTP取得に
//             "Origin: https://listenradio.jp" が要る(listenradio_client.h)。
//   kFmPlapla … FMぷらぷらの局ID(例: "radioodate")。kJcbaと同じ配信バックエンド
//             (fmplapla.com向けのselect_stream/WebSocket/Ogg-Opus)なので、
//             jcba_client.h の同じ実装をJcbaService::kFmPlaplaで呼び分ける。
//
// 列挙値は直接選局コード(下記)のプラットフォーム番号 - 1 で、kStations の並び順
// (tools/gen_station_list.py の PLATFORM_ORDER)でもある。
enum class ChannelPlatform : uint8_t {
  kNhk = 0,
  kRadiko = 1,
  kJcba = 2,
  kListenRadio = 3,
  kFmPlapla = 4,
};
constexpr size_t kChannelPlatformCount = 5;

// 局(チャンネル)1件分のデータ。実体はstation_id_list.h(tools/gen_station_list.pyが
// 生成する固定テーブル)の要素で、文字列はすべてリテラル=フラッシュ常駐。
// 以前は起動時に取得したXMLから固定長バッファへコピーしていたが、一覧が完全に
// 静的になったためコピーは不要になった(全国分のRAMを丸ごと節約できる)。
struct Channel {
  const char* name;
  const char* streamId;
  uint8_t area;  // 所在エリア(JPnのn = 都道府県のJISコード)。NHKは配信拠点の所在県。
  ChannelPlatform platform;

  const char* getName() const { return name; }
  const char* getStreamId() const { return streamId; }
  ChannelPlatform getPlatform() const { return platform; }
  uint8_t getArea() const { return area; }
};

// kStations 上でプラットフォームが占める連続区間 [first, first + count)。
// station_id_list.h の kPlatformRanges[] の要素で、添字は ChannelPlatform の列挙値。
struct PlatformRange {
  uint16_t first;
  uint16_t count;
};

// 固定テーブル上の「現在選局中のインデックス」を保持する。一覧そのものは
// station_id_list.hのkStationsで、実行時に構築も変更もしない。
//
// テーブルはプラットフォーム→エリア→局名の順に並ぶ(同じ局を複数エリアへ複製しない)。
// ユーザーに見せる局の番号は、フラットな添字ではなく「直接選局コード」:
//   P × 1000 + NNN   P   = プラットフォーム番号(ChannelPlatformの列挙値 + 1。1=NHK,
//                          2=radiko, 3=JCBA, 4=ListenRadio, 5=FMぷらぷら)
//                    NNN = そのプラットフォーム内の1始まりの局番号(テーブル上の
//                          プラットフォーム先頭からのオフセット + 1)
// 例: 2013 = radikoの13局目。テンキー直接選局(command.cpp)はこの4桁を受け取る。
// 添字(index)はあくまで内部表現で、channel_store の保存値もこちら。
class ChannelList {
 public:
  size_t count() const;

  // 範囲外はnullptrを返す。
  const Channel* getChannel(size_t index) const;

  // 範囲外は無視する。
  void setCurrentIndex(size_t index);
  size_t getCurrentIndex() const { return currentIndex_.load(std::memory_order_relaxed); }

  // 直接選局コード(上記の P×1000+NNN)⇔ テーブルの添字。コードが割り当てられて
  // いない(プラットフォーム番号が範囲外・局番号が0または局数超え)なら -1。
  int indexForTuneCode(int code) const;
  // 範囲外の添字なら 0(有効なコードは必ず1000以上)。ログ用。
  int tuneCodeForIndex(size_t index) const;

  // 現在の局と同じプラットフォームの中で、次/前のエリアの先頭局へ移動して、その
  // インデックスを返す。プラットフォームの末尾/先頭で同じプラットフォーム内へ回り込む
  // (他のプラットフォームへは行かない)。テーブルがプラットフォーム内でエリア順に
  // 並んでいることを前提にした線形探索。全国分を1局ずつ送るのは現実的ではないため、
  // エリア単位の粗い移動手段を用意している。
  int nextAreaFirstIndex() const;
  int previousAreaFirstIndex() const;

  // 次/前のプラットフォームの先頭局のインデックスを返す(末尾/先頭で回り込む)。
  // 局が0のプラットフォームは飛ばす。
  int nextPlatformFirstIndex() const;
  int previousPlatformFirstIndex() const;

  // 現在のエリアの表示名(例: "東京")。不明なら空文字。
  const char* getCurrentAreaName() const;

  // index番目の局を無料(非プレミアム)で再生できるエリアのビットマスク
  // (station_id_list.hのkStationFreeAreaMask)。bit (JISコード-1) が立っていれば
  // そのエリアの無料リスナーが再生できる。NHK行・プレミアム専用局・範囲外は0。
  // radiko_client.hのradikoStationPlayable()へ渡す。
  uint64_t getFreeAreaMask(size_t index) const;

 private:
  // 書き込みはradio_stream.cppのswitchToChannel()(ネットワーク取得タスク, core0)、
  // 読み出しはcommand.cpp / radio_stream.cppの公開API(loop(), core1)なので
  // コアをまたぐ。値は単一ワードで、これに紐づいて公開する他のデータは無い
  // (参照先のkStationsは不変テーブル)ため、relaxedで十分。
  std::atomic<size_t> currentIndex_{0};
};

// このアプリではインスタンスは常に1つだけなので、グローバルに1つ持つ。
extern ChannelList g_channelList;
