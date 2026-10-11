// モジュールのログ設定はlog_config.hのincludeより前に定義する。
#define LOG_MODULE_LEVEL LOG_LEVEL_PLS
#define LOG_MODULE_TAG "PLS"
#include "hls_playlist.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <cstdio>
#include <ctime>

#include "gzip_inflate.h"
#include "log_config.h"

namespace hls {

namespace {

String schemeAndHost(const String& url) {
  int schemeEnd = url.indexOf("://");
  if (schemeEnd < 0) {
    return "";
  }
  int pathStart = url.indexOf('/', schemeEnd + 3);
  if (pathStart < 0) {
    return url;
  }
  return url.substring(0, pathStart);
}

// 呼び出し元(radio_stream.cpp)が持つ共有client/httpを使い回す。ホストが変わる場合
// (master→media切り替え時)は明示的に切断してから繋ぎ直す(HTTPClientは接続中の
// ソケットのホストが変わったかどうかまでは見てくれないため)。
String s_lastHost;

String extractHost(const String& url) {
  int schemeEnd = url.indexOf("://");
  if (schemeEnd < 0) {
    return "";
  }
  int pathStart = url.indexOf('/', schemeEnd + 3);
  return (pathStart < 0) ? url.substring(schemeEnd + 3) : url.substring(schemeEnd + 3, pathStart);
}

bool httpGetText(WiFiClientSecure& client, HTTPClient& http, const String& url, String& out,
                 const String& radikoAuthToken, const String& originHeader) {
  String host = extractHost(url);
  if (host != s_lastHost && client.connected()) {
    client.stop();
  }
  s_lastHost = host;

  if (!http.begin(client, url)) {
    LOGE("http.begin failed for %s", url.c_str());
    return false;
  }
  if (radikoAuthToken.length() > 0) {
    http.addHeader("X-Radiko-AuthToken", radikoAuthToken);
  }
  if (originHeader.length() > 0) {
    http.addHeader("Origin", originHeader);
    // addHeader("Accept-Encoding", ...)はArduino HTTPClientが「コードが管理するヘッダ」
    // として黙って無視する(HTTPClient.cppのaddHeader()参照)ので、専用のsetAcceptEncoding()
    // を使う。ただし実機で確認したところListenRadioの配信サーバはこれも無視して常時gzipで
    // 返してくる(Accept-Encoding非送出のcurlでも同じ)。無駄でも「gzipを望んでいない」意思
    // 表示として残しつつ、下のgzipInflateIfNeeded()で実際にどう返ってきても対応する。
    http.setAcceptEncoding("identity");
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    LOGE("GET %s failed, code=%d", url.c_str(), code);
    http.end();
    return false;
  }
  out = http.getString();
  http.end();

  // ListenRadioはAccept-Encodingの指定を無視してgzip圧縮した本文を返すことがある
  // (先頭2バイトが1F 8B)。プレイリストは小さいので、静的バッファへ展開してから
  // 差し替える。展開先に収まらない/gzipでない場合はそのまま(非gzipならno-op)。
  if (static_cast<size_t>(out.length()) >= 2 && static_cast<uint8_t>(out[0]) == 0x1F &&
      static_cast<uint8_t>(out[1]) == 0x8B) {
    // 実測でchunklist.m3u8は展開後200バイト前後。他局のmaster/mediaプレイリストも
    // 数十局分のセグメント一覧を持つ大きめのもの(NHK/radiko)を想定しても数KB程度なので、
    // このアプリのメモリ予算([[project_m5rajiru_hls_radio]]参照)に配慮して控えめに確保する。
    static uint8_t s_inflateBuf[2048];
    size_t compressedLen = out.length();
    size_t inflatedLen = 0;
    if (gzipInflateIfNeeded(reinterpret_cast<const uint8_t*>(out.c_str()), compressedLen, s_inflateBuf,
                            sizeof(s_inflateBuf), &inflatedLen)) {
      out = String(reinterpret_cast<const char*>(s_inflateBuf), inflatedLen);
      LOGD("gzip response inflated: %u -> %u bytes", static_cast<unsigned>(compressedLen),
           static_cast<unsigned>(inflatedLen));
    } else {
      LOGE("gzip response failed to inflate (%u bytes compressed)", static_cast<unsigned>(compressedLen));
    }
  }
  return true;
}

// "KEY=\"value\"" 形式の属性からvalueを抜き出す。見つからなければ空文字。
String extractQuoted(const String& line, const char* key) {
  int keyPos = line.indexOf(key);
  if (keyPos < 0) {
    return "";
  }
  int firstQuote = line.indexOf('"', keyPos);
  if (firstQuote < 0) {
    return "";
  }
  int secondQuote = line.indexOf('"', firstQuote + 1);
  if (secondQuote < 0) {
    return "";
  }
  return line.substring(firstQuote + 1, secondQuote);
}

String trimLine(const String& line) {
  String s = line;
  s.trim();
  return s;
}

// radiko の #EXT-X-PROGRAM-DATE-TIME(例 "2026-09-02T16:16:10.074+09:00")を
// エポック秒へ。オフセットは常に +09:00 で、システムTZも JST-9 前提なので
// (このプロジェクト全体の既存の前提。radiko_client.cpp / command.cpp と同じ)
// 壁時計フィールドをそのまま mktime に渡す。末尾のオフセットと小数秒は読まない
// (小数秒切り捨て = 頭が実サンプルより最大1s手前になるだけで安全側)。
// パース失敗は 0。
time_t parseProgramDateTime(const char* s) {
  int year = 0, mon = 0, day = 0, hour = 0, min = 0, sec = 0;
  if (sscanf(s, "%d-%d-%dT%d:%d:%d", &year, &mon, &day, &hour, &min, &sec) != 6) {
    return 0;
  }
  struct tm tm = {};
  tm.tm_year = year - 1900;
  tm.tm_mon = mon - 1;
  tm.tm_mday = day;
  tm.tm_hour = hour;
  tm.tm_min = min;
  tm.tm_sec = sec;
  tm.tm_isdst = 0;
  time_t t = mktime(&tm);
  return (t < 0) ? 0 : t;
}

}  // namespace

String resolveUrl(const String& base, const String& ref) {
  if (ref.startsWith("http://") || ref.startsWith("https://")) {
    return ref;
  }
  if (ref.startsWith("/")) {
    return schemeAndHost(base) + ref;
  }
  int lastSlash = base.lastIndexOf('/');
  if (lastSlash < 0) {
    return ref;
  }
  return base.substring(0, lastSlash + 1) + ref;
}

bool fetchMasterPlaylist(WiFiClientSecure& client, HTTPClient& http, const String& masterUrl,
                         String& outMediaPlaylistUrl, const String& radikoAuthToken, const String& originHeader) {
  String body;
  if (!httpGetText(client, http, masterUrl, body, radikoAuthToken, originHeader)) {
    return false;
  }

  int pos = 0;
  bool sawStreamInf = false;
  while (pos < static_cast<int>(body.length())) {
    int nl = body.indexOf('\n', pos);
    String rawLine = (nl < 0) ? body.substring(pos) : body.substring(pos, nl);
    pos = (nl < 0) ? body.length() : nl + 1;

    String line = trimLine(rawLine);
    if (line.length() == 0) {
      continue;
    }
    if (line.startsWith("#EXT-X-STREAM-INF")) {
      sawStreamInf = true;
      continue;
    }
    if (sawStreamInf && !line.startsWith("#")) {
      outMediaPlaylistUrl = resolveUrl(masterUrl, line);
      return true;
    }
  }

  LOGE("no variant found in master playlist");
  return false;
}

bool fetchMediaPlaylist(WiFiClientSecure& client, HTTPClient& http, const String& mediaUrl, MediaPlaylist& out,
                        const String& radikoAuthToken, const String& originHeader) {
  String body;
  if (!httpGetText(client, http, mediaUrl, body, radikoAuthToken, originHeader)) {
    return false;
  }

  out = MediaPlaylist();

  bool haveFirstSequence = false;
  bool pendingSegment = false;
  // PROGRAM-DATE-TIME は直後のセグメントに掛かる(EXTINFの前に来る)。
  time_t pendingProgramDateTime = 0;
  uint32_t pendingDurationMs = 0;

  int pos = 0;
  while (pos < static_cast<int>(body.length())) {
    int nl = body.indexOf('\n', pos);
    String rawLine = (nl < 0) ? body.substring(pos) : body.substring(pos, nl);
    pos = (nl < 0) ? body.length() : nl + 1;

    String line = trimLine(rawLine);
    if (line.length() == 0) {
      continue;
    }

    if (line.startsWith("#EXT-X-MEDIA-SEQUENCE:")) {
      out.firstSequence = strtoull(line.c_str() + strlen("#EXT-X-MEDIA-SEQUENCE:"), nullptr, 10);
      haveFirstSequence = true;
    } else if (line.startsWith("#EXT-X-TARGETDURATION:")) {
      out.targetDurationSec = atoi(line.c_str() + strlen("#EXT-X-TARGETDURATION:"));
    } else if (line.startsWith("#EXT-X-KEY:")) {
      if (line.indexOf("METHOD=AES-128") >= 0) {
        String keyRef = extractQuoted(line, "URI=");
        if (keyRef.length() > 0) {
          out.keyUrl = resolveUrl(mediaUrl, keyRef);
        }
      }
    } else if (line.startsWith("#EXT-X-PROGRAM-DATE-TIME:")) {
      pendingProgramDateTime = parseProgramDateTime(line.c_str() + strlen("#EXT-X-PROGRAM-DATE-TIME:"));
    } else if (line.startsWith("#EXT-X-ENDLIST")) {
      out.endList = true;
    } else if (line.startsWith("#EXTINF:")) {
      double sec = strtod(line.c_str() + strlen("#EXTINF:"), nullptr);
      pendingDurationMs = sec > 0 ? static_cast<uint32_t>(sec * 1000.0 + 0.5) : 0;
      pendingSegment = true;
    } else if (!line.startsWith("#")) {
      if (pendingSegment) {
        Segment seg;
        seg.sequence = out.firstSequence + out.segments.size();
        seg.url = resolveUrl(mediaUrl, line);
        seg.programDateTime = pendingProgramDateTime;
        seg.durationMs = pendingDurationMs;
        out.segments.push_back(seg);
        pendingSegment = false;
        pendingProgramDateTime = 0;
        pendingDurationMs = 0;
      }
    }
  }

  if (!haveFirstSequence || out.segments.empty()) {
    // gzipで返ってきていないか(先頭2バイトが1F 8B)、空応答か、想定外の内容かを切り分ける
    // ための診断ログ。先頭32バイトだけ16進とprintable表示の両方を出す。
    char hex[3 * 32 + 1] = {0};
    char printable[32 + 1] = {0};
    size_t n = body.length() < 32 ? body.length() : 32;
    for (size_t i = 0; i < n; ++i) {
      uint8_t b = static_cast<uint8_t>(body[i]);
      snprintf(hex + i * 3, 4, "%02X ", b);
      printable[i] = (b >= 0x20 && b < 0x7F) ? static_cast<char>(b) : '.';
    }
    LOGE("media playlist parse produced no segments (body=%u bytes, head=[%s] \"%s\")",
         static_cast<unsigned>(body.length()), hex, printable);
    return false;
  }
  return true;
}

}  // namespace hls
