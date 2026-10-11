#pragma once

#include <cstdint>

// 操作パネル(タクトスイッチ/ロータリースイッチ/エンコーダ)の管理レイヤー。
// tca6424a(生ビット読み出し)とESP32直結エンコーダ(GPIO18/19)を専用タスクでポーリングし、
// デバウンス・エッジ検出・ロータリー位置変換を行ってPanelEventとしてキューへ積む。
// 配線(どのビットがどのキーか)はpanel.cpp冒頭のテーブルに集約している。

// 論理キーID。タクトスイッチ全部。
enum class PanelKey : uint8_t {
  // TCA6424A #1
  kTact1,
  kTact2,
  kTact3,
  kTact4,
  kTact5,
  kTact6,
  kTact7,
  kTact8,
  kTact9,
  kTactClear,
  kTact0,
  kTactEnter,
  // TCA6424A #2: ファンクションキー6個
  kFunc1,
  kFunc2,
  kFunc3,
  kFunc4,
  kFunc5,
  kFunc6,
  // TCA6424A #2: エンコーダ軸のpush / + / - と VOICE
  // (P20-P23の割当順は実機で要確認)
  kEncoderPush,
  kEncoderPlus,
  kEncoderMinus,
  kVoice,
  kCount
};

enum class PanelRotary : uint8_t { kSp12t = 0, kSp6t = 1, kCount };

enum class PanelEventType : uint8_t {
  kKeyDown,       // id=PanelKey
  kKeyUp,         // id=PanelKey
  kKeyLongPress,  // id=PanelKey (押下中に1回だけ)
  kRotarySelect,  // id=PanelRotary, value=位置(0始まり)
  kEncoderStep,   // id=0, value=ディテント数(正=時計回り、負=反時計回り)
};

struct PanelEvent {
  PanelEventType type;
  uint8_t id;
  int8_t value;
};

// TCA6424A・PCNT・イベントキュー・ポーリングタスクを初期化する。
// TCA6424Aが片方でも応答しなくてもタスクは起動する(戻り値はfalse)。
bool panelBegin();

// イベントを1件取り出す(非ブロッキング)。無ければfalse。
bool panelPoll(PanelEvent& ev);

// ロータリースイッチの現在位置(0始まり)。選択なし/未確定は-1。
int panelRotaryPosition(PanelRotary which);
