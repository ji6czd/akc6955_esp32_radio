#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ctime>

// radiko.jpの認証(プレミアム会員ログイン + auth1/auth2)と、ライブ配信の
// マスタープレイリストURL組み立てを担当する。
// credentials の radiko
// ID/パスワード(NVS、無ければsecrets.h)が設定されていれば認証時に自動でログインし、
// エリアフリー(居住エリア外の局の再生)が有効になる。ログインに失敗した場合や
// 認証情報が空の場合は、従来どおりエリア自動判定のみの無料利用として動作する。
//
// client/httpは呼び出し側(radio_stream.cpp)が持つ共有インスタンスをそのまま渡すこと。
// 専用のTLS接続は持たない —
// PSRAM非搭載機ではTLS接続を同時に2つ保持するとヒープが
// 尽きることを実機で確認済みのため([[project-m5rajiru-hls-radio]]参照)、
// radiko宛のリクエストも既存の「唯一の共有TLS接続」を使い回す。

// 認証済みでなければauth1→auth2を行い(未認証時のみ)、station_idからライブ配信の
// マスタープレイリストURLを組み立ててoutUrlに返す(呼ぶたびに新しいlsidを振る)。
// エリアフリーで認証できていればtype=c(全国の局が再生可能)、そうでなければtype=b
// (エリア内の局のみ。エリア外を要求すると403)でURLを作る。
// トークンの有効期限切れは今のところ検知しない(失敗したら呼び出し側でリトライする想定)。
bool radikoBuildLiveUrl(WiFiClientSecure& client, HTTPClient& http,
                        const char* stationId, String& outUrl);

// タイムフリー(過去番組)のマスタープレイリストURLを組み立ててoutUrlに返す。
// seekEpochは再生開始位置(UNIX時間)。システムTZはJST-9前提で、内部でJST壁時計の
// YYYYMMDDHHMMSSに整形する。呼び出し側でシステム時刻が同期済みであることを保証すること。
//   * 配信ホストは局ごとに v3/station/stream/pc_html5/<局>.xml の
//     <url areafree=".." timefree="1"><playlist_create_url>
//     から取得してキャッシュする (プレミアム認証時は areafree="1"
//     側)。取得失敗時は既知の定数へフォールバック。
//   * end_at/to は seekEpoch + config::kTimefreeSessionSpanSec(3時間)。
//     #EXT-X-ENDLIST はこの to
//     に達するまで付かない。付いたら呼び出し側が続きの時刻で
//     このURLを作り直す(radio_stream.cpp の continueTimefreeAfterEndList)。
//   * l= は公式ウェブアプリと同じ15秒(config::kTimefreeInitialFetchSegments)。
//   * type はライブと同じく s_premium ? "c"(エリアフリー) : "b"(在圏のみ)。
//   * 返るプレイリストの medialist はライブと同じスライディングウィンドウで、
//     #EXT-X-MEDIA-SEQUENCE
//     付き・AES無し。既存のhls_playlist/refreshPlaylistがそのまま扱える。
// 遡り限界(約1週間)や直近端(now-90〜150秒)のクランプは呼び出し側の責務。
bool radikoBuildTimefreeUrl(WiFiClientSecure& client, HTTPClient& http,
                            const char* stationId, time_t seekEpoch,
                            String& outUrl);

// 直近の認証で得たauthToken。radikoBuildLiveUrl()が一度も成功していなければ空文字。
// HLSのmaster/media playlist・セグメント取得時に X-Radiko-AuthToken
// ヘッダとして使う。
const String& radikoAuthToken();

// auth1→auth2を行い(未認証時のみ)、エリアコードを確定させる。station_idがまだ無い
// 段階(局リストXML取得等)でradikoArea()だけ先に必要な場合はこちらを直接呼ぶ
// (radikoBuildLiveUrl()も内部で同じ認証状態を共有する)。
bool radikoEnsureAuthorized(WiFiClientSecure& client, HTTPClient& http);

// 認証済みの状態を破棄する。authTokenの失効が疑われるとき(HLS取得の失敗が続いたとき等)に
// 呼び、次の radikoEnsureAuthorized() でログイン + auth1/auth2 をやり直させる。
// エリアとプレミアム判定は保持する(再認証に失敗しても事前の在圏判定が使えるように)。
void radikoInvalidateAuth();

// 直近の認証で得たエリアコード(例: "JP13"、auth2レスポンスの先頭カンマ区切り)。
// 未認証、またはレスポンスの形式が想定外だった場合は空文字。
const String& radikoArea();

// 直近の認証がエリアフリー(プレミアム)として通ったかどうか。
// ログインレスポンスのareafreeが"1"のときのみtrue(無料会員でのログイン成功はfalse)。
bool radikoIsPremium();

// 直近の認証で得たエリア番号(radikoArea()の "JPnn" の nn。例: "JP13" -> 13)。
// 未認証やレスポンスの書式が想定外なら 0。channel_list.h の Channel::area
// (JIS都道府県コード)と直接比較できる。
uint8_t radikoAreaNumber();

// 無料(非プレミアム)利用時に、その radiko 局が再生できるかどうか。
// freeAreaMask は局の「無料開放エリア」ビットマスク(channel_list.hの
// ChannelList::getFreeAreaMask() / station_id_list.hのkStationFreeAreaMask)。
//   * プレミアム(エリアフリー)認証済み    … 常に true
//   * まだ認証していない(エリア番号が 0)  … true(判定不能なので従来どおり試す)
//   * それ以外                              …
//   在圏エリアのbitがマスクに立っていれば true
// 在圏で無料開放されていない局へ type=b で接続を試みると 403
// が返り続け、呼び出し側の リトライループが抜けられず最悪 WDT
// パニックになるため、false の局へは切り替えないこと。
bool radikoStationPlayable(uint64_t freeAreaMask);
