#pragma once

#include <cstdint>

// 起動時のNTP時刻同期を担当する。端末にはRTC用のバッテリが無く、電源投入直後の
// システム時刻は1970年に張り付いているため、Wi-Fi接続後にNTPで現在時刻を取得して
// システムクロック(settimeofday)へ反映する。タイムゾーンやサーバはconfig.hを参照。

// 起動時、wifiBegin()の後に呼ぶ。時刻が入るまで最大 config::kNtpSyncTimeoutMs
// ブロッキングで待つ(起動時は他にやることが無いため許容)。
// 同期できたら true、タイムアウトしたら false を返す(呼び出し側は続行してよい)。
bool timeSyncBegin();

// 現在のシステム時刻が有効(NTP同期済み)かどうか。
bool timeSyncIsValid();
