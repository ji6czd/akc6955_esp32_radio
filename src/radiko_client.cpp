// モジュールのログ設定はlog_config.hのincludeより前に定義する。
#define LOG_MODULE_LEVEL LOG_LEVEL_RDK
#define LOG_MODULE_TAG "RDK"
#include "radiko_client.h"

#include <base64.h>
#include <esp_random.h>
#include <mbedtls/md5.h>

#include <cctype>
#include <cstring>
#include <ctime>

#include "config.h"
#include "credentials.h"
#include "log_config.h"

namespace {

constexpr const char* kAuthKey = "bcd151073c03b352e1ef2fd66c32209da9ca0afa";
constexpr const char* kAuth1Url = "https://radiko.jp/v2/api/auth1";
constexpr const char* kAuth2Url = "https://radiko.jp/v2/api/auth2";
constexpr const char* kLoginUrl =
    "https://radiko.jp/ap/member/webapi/member/login";
constexpr const char* kLiveUrlBase =
    "https://alliance-stream-radiko.smartstream.ne.jp/so/playlist.m3u8";

// タイムフリーの配信ホストは局のstream
// XMLから取るのが正だが、取得できなかったとき用の
// 既定値(2026-08時点で在圏[areafree=0]タイムフリーの playlist_create_url)。
constexpr const char* kStreamXmlBase =
    "https://radiko.jp/v3/station/stream/pc_html5/";
constexpr const char* kTimefreeFallbackBase =
    "https://tf-f-rpaa-radiko.smartstream.ne.jp/tf/playlist.m3u8";

// 局ごとのタイムフリー playlist_create_url
// を1件だけキャッシュする(直近に使った局)。
char s_tfBase[128] = "";
char s_tfBaseStation[12] = "";

String s_authToken;
String s_area;
bool s_authorized = false;

// プレミアム(エリアフリー)会員としてログインした際のセッションID。
// auth1/auth2に Cookie: radiko_session=... として渡すと、返るauthTokenが
// エリアフリー対応になり、居住エリア外の局も再生できるようになる。
// 空文字なら未ログイン(=エリア判定のみの無料利用)。
char s_radikoSession[128] = "";
bool s_premium = false;

// 乱数からlsid(32桁16進のMD5ハッシュ文字列)を作る。radiko側は「リスナーセッションID」
// として使うだけで内容自体に意味はないので、衝突しなければ何でもよい。
String makeLsid() {
  char seed[24];
  snprintf(seed, sizeof(seed), "%08x%08x", static_cast<unsigned>(esp_random()),
           static_cast<unsigned>(esp_random()));

  unsigned char digest[16];
  mbedtls_md5(reinterpret_cast<const unsigned char*>(seed), strlen(seed),
              digest);

  char hex[33];
  for (int i = 0; i < 16; ++i) {
    snprintf(hex + i * 2, 3, "%02x", digest[i]);
  }
  return String(hex);
}

// auth1/auth2はradiko.jp宛。直前のリクエスト先が別ホストの可能性があるため、
// (radiko_client.cppはhls_playlist.cppのホスト追跡状態を共有していない)
// 呼び出し前に必ず明示的に接続を切っておく。
void resetSharedConnection(WiFiClientSecure& client) {
  if (client.connected()) {
    client.stop();
  }
}

// ログイン済みならauth1/auth2にCookieヘッダを足す。未ログインなら何もしない
// (無料利用時と完全に同じリクエストになる)。
// 実測では効いているのはauth2側だけ(auth1にだけ付けてもエリアフリーにならない)だが、
// 実害が無いので両方に付けている。
void addSessionCookie(HTTPClient& http) {
  if (s_radikoSession[0] == '\0') {
    return;
  }
  http.addHeader("Cookie", String("radiko_session=") + s_radikoSession);
}

// application/x-www-form-urlencoded用のパーセントエンコード。
// メールアドレスの'@'やパスワードの記号をそのまま送ると解釈が変わりうるため、
// 非予約文字(RFC 3986のunreserved)以外はすべて%XXにする。
String formEncode(const char* s) {
  String out;
  for (const char* p = s; *p != '\0'; ++p) {
    char c = *p;
    if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      out += c;
    } else {
      char hex[4];
      snprintf(hex, sizeof(hex), "%%%02X", static_cast<unsigned char>(c));
      out += hex;
    }
  }
  return out;
}

// ログインレスポンス(JSON)から "key":"value" の value を取り出す。
// 数値で返る項目もあるので、クォートの有無どちらでも読めるようにしている。
// 見つからなければ空文字。
String extractJsonString(const String& body, const char* key) {
  String needle = String("\"") + key + "\"";
  int pos = body.indexOf(needle);
  if (pos < 0) {
    return "";
  }
  pos = body.indexOf(':', pos + needle.length());
  if (pos < 0) {
    return "";
  }
  ++pos;
  while (pos < static_cast<int>(body.length()) &&
         (body[pos] == ' ' || body[pos] == '"')) {
    ++pos;
  }
  int end = pos;
  while (end < static_cast<int>(body.length()) && body[end] != '"' &&
         body[end] != ',' && body[end] != '}') {
    ++end;
  }
  String value = body.substring(pos, end);
  value.trim();
  return value;
}

// プレミアム会員としてログインし、radiko_sessionを取得する。
// 失敗しても致命的ではない(無料=エリア内のみの利用として続行する)ので、
// 呼び出し側は戻り値falseでも認証処理そのものは継続してよい。
bool doLogin(WiFiClientSecure& client, HTTPClient& http) {
  const char* radikoId = credentialsRadikoId();
  const char* radikoPassword = credentialsRadikoPassword();
  if (radikoId[0] == '\0' || radikoPassword[0] == '\0') {
    return false;
  }

  resetSharedConnection(client);
  if (!http.begin(client, kLoginUrl)) {
    LOGE("login http.begin failed");
    return false;
  }
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String form =
      "mail=" + formEncode(radikoId) + "&pass=" + formEncode(radikoPassword);
  int code = http.POST(form);
  String body = http.getString();
  http.end();

  if (code != HTTP_CODE_OK) {
    LOGE("login failed, code=%d", code);
    return false;
  }

  String session = extractJsonString(body, "radiko_session");
  if (session.length() == 0 || session.length() >= sizeof(s_radikoSession)) {
    LOGE("login response has no usable radiko_session");
    return false;
  }
  strncpy(s_radikoSession, session.c_str(), sizeof(s_radikoSession) - 1);
  s_radikoSession[sizeof(s_radikoSession) - 1] = '\0';

  // areafreeが"1"ならエリアフリー(プレミアム)。無料会員でもログインは通るので、
  // ここで区別しておく。
  s_premium = (extractJsonString(body, "areafree") == "1");
  LOGI("login ok, areafree=%d", s_premium ? 1 : 0);
  return true;
}

bool doAuth1(WiFiClientSecure& client, HTTPClient& http, String& outToken,
             int& outKeyOffset, int& outKeyLength) {
  resetSharedConnection(client);
  const char* headerKeys[] = {"x-radiko-authtoken", "x-radiko-keylength",
                              "x-radiko-keyoffset"};
  http.collectHeaders(headerKeys, 3);

  if (!http.begin(client, kAuth1Url)) {
    LOGE("auth1 http.begin failed");
    return false;
  }
  http.addHeader("X-Radiko-App-Version", "0.0.1");
  http.addHeader("X-Radiko-App", "pc_html5");
  http.addHeader("X-Radiko-Device", "pc");
  http.addHeader("X-Radiko-User", "dummy_user");
  addSessionCookie(http);

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    LOGE("auth1 failed, code=%d", code);
    http.end();
    return false;
  }
  outToken = http.header("x-radiko-authtoken");
  outKeyLength = http.header("x-radiko-keylength").toInt();
  outKeyOffset = http.header("x-radiko-keyoffset").toInt();
  http.end();
  return outToken.length() > 0 && outKeyLength > 0;
}

bool doAuth2(WiFiClientSecure& client, HTTPClient& http, const String& token,
             const String& partialKeyB64) {
  resetSharedConnection(client);
  if (!http.begin(client, kAuth2Url)) {
    LOGE("auth2 http.begin failed");
    return false;
  }
  http.addHeader("X-Radiko-App-Version", "0.0.1");
  http.addHeader("X-Radiko-App", "pc_html5");
  http.addHeader("X-Radiko-Device", "pc");
  http.addHeader("X-Radiko-User", "dummy_user");
  http.addHeader("X-Radiko-AuthToken", token);
  http.addHeader("X-Radiko-Partialkey", partialKeyB64);
  addSessionCookie(http);

  int code = http.GET();
  String body = http.getString();
  http.end();
  if (code != HTTP_CODE_OK) {
    LOGE("auth2 failed, code=%d", code);
    return false;
  }
  int sep = body.indexOf(',');
  if (sep > 0) {
    s_area = body.substring(0, sep);
    LOGI("auth2 ok: %s (premium=%d)", s_area.c_str(), s_premium ? 1 : 0);
  } else {
    // エリアが取れなくても認証自体は成立している。radikoAreaNumber() が 0
    // を返し、 radikoStationPlayable() は判定不能として従来どおり全局を試す。
    LOGW("auth2 ok but no area in response (premium=%d)", s_premium ? 1 : 0);
  }
  return true;
}

bool ensureAuthorized(WiFiClientSecure& client, HTTPClient& http) {
  if (s_authorized) {
    return true;
  }

  // auth1より前にログインしておく。失敗しても無料利用(エリア内のみ)として続行する。
  if (s_radikoSession[0] == '\0') {
    if (!doLogin(client, http)) {
      LOGW("premium login failed, continuing as free listener");
    }
  }

  String token;
  int keyOffset = 0;
  int keyLength = 0;
  if (!doAuth1(client, http, token, keyOffset, keyLength)) {
    return false;
  }

  String authKey(kAuthKey);
  String partialKey = authKey.substring(keyOffset, keyOffset + keyLength);
  String partialKeyB64 = base64::encode(partialKey);

  if (!doAuth2(client, http, token, partialKeyB64)) {
    return false;
  }

  s_authToken = token;
  s_authorized = true;
  return true;
}

// stream XML本文から、areafree が premium に一致し timefree="1" の <url>
// ブロック内の <playlist_create_url>
// を取り出す。属性の並び順に依存しないよう、開きタグ文字列に
// 対して両方の属性を含むか調べる。見つからなければ空文字。
String pickPlaylistCreateUrl(const String& xml, bool premium) {
  const char* wantAreafree = premium ? "areafree=\"1\"" : "areafree=\"0\"";
  int pos = 0;
  while (true) {
    int u = xml.indexOf("<url ", pos);
    if (u < 0) {
      return "";
    }
    int tagEnd = xml.indexOf('>', u);
    if (tagEnd < 0) {
      return "";
    }
    String openTag = xml.substring(u, tagEnd);
    if (openTag.indexOf("timefree=\"1\"") >= 0 &&
        openTag.indexOf(wantAreafree) >= 0) {
      int p = xml.indexOf("<playlist_create_url>", tagEnd);
      int blockEnd = xml.indexOf("</url>", tagEnd);
      if (p >= 0 && (blockEnd < 0 || p < blockEnd)) {
        p += static_cast<int>(strlen("<playlist_create_url>"));
        int e = xml.indexOf("</playlist_create_url>", p);
        if (e > p) {
          String url = xml.substring(p, e);
          url.trim();
          return url;
        }
      }
    }
    pos = tagEnd + 1;
  }
}

// 局のタイムフリー配信ベースURL(playlist_create_url)を得る。直近に使った1局分だけ
// キャッシュする。取得・パースに失敗したらfalse(呼び出し側で定数へフォールバック)。
bool fetchTimefreeBase(WiFiClientSecure& client, HTTPClient& http,
                       const char* stationId, String& outBase) {
  if (s_tfBase[0] != '\0' && strcmp(s_tfBaseStation, stationId) == 0) {
    outBase = s_tfBase;
    return true;
  }

  resetSharedConnection(client);
  String url = String(kStreamXmlBase) + stationId + ".xml";
  if (!http.begin(client, url)) {
    LOGE("stream xml http.begin failed");
    return false;
  }
  int code = http.GET();
  String body = (code == HTTP_CODE_OK) ? http.getString() : String();
  http.end();
  if (code != HTTP_CODE_OK) {
    LOGW("stream xml GET failed, code=%d", code);
    return false;
  }

  String base = pickPlaylistCreateUrl(body, s_premium);
  if (base.length() == 0 || base.length() >= sizeof(s_tfBase)) {
    LOGW("stream xml has no usable playlist_create_url (premium=%d)",
         s_premium ? 1 : 0);
    return false;
  }
  strncpy(s_tfBase, base.c_str(), sizeof(s_tfBase) - 1);
  s_tfBase[sizeof(s_tfBase) - 1] = '\0';
  strncpy(s_tfBaseStation, stationId, sizeof(s_tfBaseStation) - 1);
  s_tfBaseStation[sizeof(s_tfBaseStation) - 1] = '\0';
  outBase = s_tfBase;
  LOGI("timefree base for %s: %s", stationId, s_tfBase);
  return true;
}

}  // namespace

bool radikoBuildLiveUrl(WiFiClientSecure& client, HTTPClient& http,
                        const char* stationId, String& outUrl) {
  if (!ensureAuthorized(client, http)) {
    return false;
  }
  String lsid = makeLsid();

  // typeパラメータが再生できる範囲を決める。実測(2026-08-28)では:
  //   type=b … 現在のエリア内の局のみ。エリア外の局は403 forbidden。
  //   type=c …
  //   エリアフリー。ただしauth2をradiko_session付きで通したトークンが必要で、
  //            未ログインのトークンでtype=cを投げても403になる。
  // つまりエリア外の局が403だったのは配信ホストの違いではなくこのパラメータが原因。
  // ホストはエリア内/外ともkLiveUrlBaseのままでよいことも確認済み。
  const char* type = s_premium ? "c" : "b";
  const int liveLenSec =
      config::kLiveInitialFetchSegments * config::kRadikoSegmentDurationSec;
  outUrl = String(kLiveUrlBase) + "?station_id=" + stationId +
           "&l=" + String(liveLenSec) + "&lsid=" + lsid + "&type=" + type;
  return true;
}

bool radikoBuildTimefreeUrl(WiFiClientSecure& client, HTTPClient& http,
                            const char* stationId, time_t seekEpoch,
                            String& outUrl) {
  if (!ensureAuthorized(client, http)) {
    return false;
  }

  String base;
  if (!fetchTimefreeBase(client, http, stationId, base)) {
    base = kTimefreeFallbackBase;
  }

  // システムTZはJST-9(config::kNtpTimezone)なので、localtime_rでそのままJST壁時計になる。
  time_t toEpoch = seekEpoch + config::kTimefreeSessionSpanSec;
  struct tm tmv{};
  char ftBuf[16];
  char toBuf[16];
  localtime_r(&seekEpoch, &tmv);
  strftime(ftBuf, sizeof(ftBuf), "%Y%m%d%H%M%S", &tmv);
  localtime_r(&toEpoch, &tmv);
  strftime(toBuf, sizeof(toBuf), "%Y%m%d%H%M%S", &tmv);

  String lsid = makeLsid();
  const char* type = s_premium ? "c" : "b";
  const int tfLenSec =
      config::kTimefreeInitialFetchSegments * config::kRadikoSegmentDurationSec;
  // start_at=seek=再生開始位置, ft/to=セッションの範囲。medialistはこのseekから
  // l秒ぶんのスライディングウィンドウを返し、再ポーリングで1×前進する。
  outUrl = base + "?station_id=" + stationId + "&start_at=" + ftBuf +
           "&ft=" + ftBuf + "&end_at=" + toBuf + "&to=" + toBuf +
           "&seek=" + ftBuf + "&l=" + String(tfLenSec) + "&lsid=" + lsid +
           "&type=" + type;
  return true;
}

const String& radikoAuthToken() { return s_authToken; }

bool radikoEnsureAuthorized(WiFiClientSecure& client, HTTPClient& http) {
  return ensureAuthorized(client, http);
}

void radikoInvalidateAuth() {
  s_authorized = false;
  s_authToken = "";
  // セッションも切れている可能性があるので、次の認証でログインからやり直す。
  // s_areaとs_premiumは、再認証が失敗しても在圏判定が使えるよう残す。
  s_radikoSession[0] = '\0';
}

const String& radikoArea() { return s_area; }

bool radikoIsPremium() { return s_premium; }

uint8_t radikoAreaNumber() {
  // s_area は auth2 レスポンス先頭の "JPnn"。先頭の英字を飛ばして数値部を読む。
  if (s_area.length() < 3) {
    return 0;
  }
  long n = s_area.substring(2).toInt();
  return (n > 0 && n <= 255) ? static_cast<uint8_t>(n) : 0;
}

bool radikoStationPlayable(uint64_t freeAreaMask) {
  if (s_premium) {
    return true;
  }
  uint8_t home = radikoAreaNumber();
  if (home == 0) {
    return true;  // 認証前 / 書式が想定外。判定できないので従来どおり試す。
  }
  return (freeAreaMask >> (home - 1)) & 1ull;
}
