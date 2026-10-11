#pragma once

// Wi-Fi / radiko の認証情報を一元管理する。
//
// 実体はNVS(Preferences, namespace "creds")に置き、起動時にstaticバッファへ
// 読み込む。NVSが空のキーは、ローカルに任意で置ける secrets.h のビルド時
// デフォルトへフォールバックする(secrets.h が無ければ空文字)。
//
// 設定の書き換えはsetup_portal(SoftAP + Webフォーム)から credentialsSave() で行う。
// フラッシュ書き換え(ファーム更新)ではNVSは消えないため、一度設定すれば再書き込み後も
// 認証情報は残る。SSIDが空(初回起動)なら main.cpp が自動でポータルへ入る。

// 起動時に一度だけ呼ぶ。NVSから読み込む。
// キーが存在しない場合のみ secrets.h の値(無ければ"")へフォールバックする。
// namespace未作成(初回起動)は正常。以下のいずれかで false を返す:
//   * NVSのオープンに失敗した(NOT_FOUND=namespace未作成は除く)
//   * キーは存在するのに読み出しに失敗した(NVS異常 / 型不一致 / バッファ不足)
// 呼び出し側は false なら fatalHalt() すること。
bool credentialsBegin();

// 以降のゲッターは常に非nullの'\0'終端文字列を返す(未設定なら "")。
// 返すポインタはcredentialsSave()を呼ぶまで有効。
const char* credentialsWifiSsid();
const char* credentialsWifiPassword();
const char* credentialsRadikoId();
const char* credentialsRadikoPassword();

// 4項目をNVSへ書き込み、staticバッファも即時更新する。
// ssidが空の場合は書き込まずfalseを返す(Wi-Fi接続不能になるのを防ぐ)。
// radiko IDやパスワードの空は許容する(プレミアムログインを行わないだけ)。
// 各値は内部バッファ長を超える分を切り詰める。
bool credentialsSave(const char* ssid, const char* password, const char* radikoId, const char* radikoPassword);
