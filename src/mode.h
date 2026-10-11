#pragma once

#include "panel.h"

// 動作モード(DSP6955ラジオ / インターネットラジオ など)の共通インターフェース。
// 切替はmode_manager.cppが行い、モード固有のキー割当・状態はそれぞれのモードが持つ。
class Mode {
 public:
  virtual ~Mode() = default;

  virtual const char* name() const = 0;

  // このモードに入る。variantはロータリー位置ごとの引数(例: DSP6955のバンド)。
  virtual void enter(int variant) = 0;

  // このモードを抜ける。再生の停止・電源断など、他モードと競合する資源を解放する。
  virtual void exit() = 0;

  // trueなら、エンコーダの回転(kEncoderStep)をこのモードのonEventで受ける。
  // falseなら共通処理で音量の増減に使う。
  virtual bool handlesEncoder() const { return false; }

  // 共通処理(音量など)で消費されなかったパネルイベントが届く。
  virtual void onEvent(const PanelEvent& ev) = 0;

  // loop()から毎周呼ばれる。
  virtual void tick() {}
};
