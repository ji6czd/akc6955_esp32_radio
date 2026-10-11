#pragma once

// Wi-Fi接続の確立・維持を担当する。認証情報は credentials(NVS、無ければsecrets.h)から取る。

// 起動時に呼ぶ。接続できるまでブロッキングでリトライする(起動時は他にやることが無いため許容)。
void wifiBegin();

// loop() から毎回呼ぶ軽量チェック。切断中のみバックオフ付きで再接続を試みる。
// 呼び出し自体は長時間ブロックしない。
bool wifiEnsureConnected();

// 連続失敗によるエスカレーション用。明示的に切断してから再接続する(ブロッキング)。
void wifiForceReconnect();
