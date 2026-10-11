#pragma once

#include <cstdint>

// ボード上の2個のTCA6424A(24bit I2C GPIOエキスパンダ)から、入力ピンのレベルを読むだけの
// 薄いドライバ。スイッチ類の意味づけ(どのビットがどのキーか、押下エッジ、デバウンス、
// ロータリースイッチの位置など)は上位レイヤーが行う。
//
//   device 0: I2Cアドレス0x22(ADDR=GND)  テンキー12個 + SP12T ロータリースイッチ
//   device 1: I2Cアドレス0x23(ADDR=VCC)  SP6T ロータリースイッチ + ファンクションキー6個
//                                         + エンコーダ押し込み/プラス/マイナス/VOICE
//
// ビット配置: bit(port * 8 + pin) = P<port><pin> (P00=bit0 ... P07=bit7, P10=bit8 ... P27=bit23)。
// スイッチは10kΩでプルアップされGNDへ落ちる配線なので、端子のレベルはLOWが「押されている」。
// 読み出し値はTCA6424Aの極性反転レジスタで反転してあり、押されている/選択されている
// ビットが1になる(端子がLOWなら1、HIGHなら0)。

constexpr int kTca6424aDeviceCount = 2;
constexpr uint32_t kTca6424aPinMask = 0x00FFFFFF;

// I2Cバスを初期化し、全ピンを入力(極性反転あり)に設定する。2個とも応答すればtrue。
// 片方でもNACKならfalse(応答したほうは使える)。
bool tca6424aBegin();

// deviceの24ピンのレベルを*bitsへ読む(上位8ビットは常に0)。成功でtrue。
// 返す値は上記のとおり反転済み(押されているビットが1)。
// 入力レジスタの読み出しでデバイス側の割り込みフラグもクリアされる。
bool tca6424aRead(const int device, uint32_t& bits);
