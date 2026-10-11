#pragma once

#include "panel.h"

// 動作モードの切替と、パネルイベントの振り分けを担当する。
// SP6Tロータリー(PanelRotary::kSp6t)の位置でモードを決める:
//   0,1,2 = AK6955ラジオ(バンド0,1,2) / 3 = インターネットラジオ / 4,5 =
//   未割り当て
// エンコーダの回転(音量)のような全モード共通の操作はここで処理し、
// それ以外のイベントは現在のモードへ渡す。

// 起動時に呼ぶ。ロータリーの現在位置に対応するモードへ入る
// (パネルは起動時にイベントを出さないため)。Wi-Fi・時刻同期の後に呼ぶこと。
void modeManagerBegin();

// loop()でpanelPoll()したイベントごとに呼ぶ。
void modeManagerHandleEvent(const PanelEvent& ev);

// loop()から毎周、イベントを処理し終えた後に呼ぶ。保留中のモード切替を実行し、
// 現在のモードのtick()を呼ぶ。
void modeManagerTick();
