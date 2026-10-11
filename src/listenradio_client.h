#pragma once

#include <Arduino.h>

// ListenRadio(smartstream.ne.jp)のライブHLS URLを組み立てる。
// 認証もトークンも不要で、URLは
//   https://mtist.as.smartstream.ne.jp/<ChannelId>/livestream/playlist.m3u8
// の形しか無い(番組表JSONのChannelHlsはhttp://だが、httpsでも同じものが返る。
// 共有TLSクライアント1本で済ませるため、他局と同じくhttpsで統一する)。
// ChannelIdだけが局ごとに違うので、局テーブル(station_id_list.h)にはその数値
// 文字列だけを持たせ、ここで実URLに展開する。NHKが番号からURLを組み立てるのと同じ考え方。
//
// ただし配信サーバはOriginヘッダをCORSとして必須で見ており(Refererは無関係)、
// 無いと403になる。master/media playlist・セグメント取得のすべてで
// "Origin: https://listenradio.jp" ヘッダを付けること(radio_stream.cpp側で付与)。
constexpr const char kListenRadioLiveUrlPrefix[] = "https://mtist.as.smartstream.ne.jp/";
constexpr const char kListenRadioLiveUrlSuffix[] = "/livestream/playlist.m3u8";
constexpr const char kListenRadioOrigin[] = "https://listenradio.jp";

// streamIdは "30011" のようなChannelIdの文字列(Channel::streamIdに入っている)。
inline String listenRadioBuildLiveUrl(const char* streamId) {
  return String(kListenRadioLiveUrlPrefix) + streamId + kListenRadioLiveUrlSuffix;
}
