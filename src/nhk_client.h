#pragma once

#include <Arduino.h>

// NHKらじる★らじるのライブ配信URLを組み立てる。
// NHKは認証もトークンも不要で、URLは
//   https://simul2.drdi.st.nhk/live/<ストリーム番号>/joined/master.m3u8
// の形しか無い。違うのは中央の番号(拠点×R1/FMごとに振られている)だけなので、
// 局テーブル(station_id_list.h)にはその番号だけを文字列で持たせ、ここで実URLに
// 展開する。radikoが局IDからURLを組み立てるのと同じ考え方。
// 番号はtools/gen_station_list.pyがNHKのconfig_web.xmlから抜き出して埋め込む。

constexpr const char kNhkLiveUrlPrefix[] = "https://simul2.drdi.st.nhk/live/";
constexpr const char kNhkLiveUrlSuffix[] = "/joined/master.m3u8";

// streamIdは "6" のようなストリーム番号の文字列(Channel::streamUrlに入っている)。
inline String nhkBuildLiveUrl(const char* streamId) { return String(kNhkLiveUrlPrefix) + streamId + kNhkLiveUrlSuffix; }
