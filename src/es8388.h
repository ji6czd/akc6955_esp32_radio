#pragma once

#include <driver/i2s_std.h>

#include <cstdint>

// ES8388コーデック(カスタムESP32オーディオボード搭載)のI2Cブリングアップを担当する。
// I2Cアドレス0x11(CEピンをHIGH)。スレーブモード・I2S
// 16bit・MCLK=256fsのDAC再生専用。

// コーデックを起動する。MCLK/BCLK/LRCKが出ている状態で呼ぶこと
// (i2sSpeakerBegin()でI2Sをenableした後)。
// outVolume: LOUT1/ROUT1/LOUT2/ROUT2の出力ボリューム(0x00=-45dB ... 0x1E=0dB,
// 0x21=+4.5dB)。
bool es8388Begin(uint8_t outVolume);

// LOUT1/ROUT1/LOUT2/ROUT2の出力ボリュームを書き込む(範囲外は0x21へ丸める)。
void es8388SetVolume(uint8_t outVolume);

// 直近にes8388Begin()/es8388SetVolume()で書き込んだ値を返す(キャッシュ)。
uint8_t es8388GetVolume();

// LIN1/RIN1のアナログ入力(AK6955の出力)を出力ミキサーへ混ぜるかを切り替える。
// 音量は出力ボリューム(es8388SetVolume)がそのまま効く。
void es8388SetLineInEnabled(bool enabled);

// ボードの配線(MCLK=GPIO0, BCK=GPIO5, WS=GPIO25, DOUT=GPIO26)でI2S
// TXチャンネルを
// 生成・std_mode初期化する(enable前の状態で返す)。失敗時はnullptrを返す。
i2s_chan_handle_t es8388CreateI2sTxChannel();

// PAM8406のMUTEピン(GPIO32)を制御する。trueでミュート解除(HIGH)。
void es8388SetAmpEnabled(bool enabled);
