#pragma once

#include <cstddef>
#include <cstdint>

// 内部I2Cバス(ES8388コーデック等が繋がる)を一元管理する。ピン/クロックはここだけが
// 知っていればよい情報なのでヘッダには出さない。複数タスクから呼ばれる前提で、
// 1レジスタアクセス(read/write)単位での排他制御をここで行う。

// I2Cバスを初期化する。各デバイスドライバのbegin()から呼ばれる想定(複数回呼んでも
// 安全: 2回目以降はWire.begin()が同一設定なら無害、ミューテックスも1回しか作らない)。
void i2cManagerBegin();

// devAddr宛にreg, valueの2バイトを書き込む。内部で排他制御する。
bool i2cWriteReg8(uint8_t devAddr, uint8_t reg, uint8_t value);

// devAddrのregレジスタを読み出し、*valueに格納する。内部で排他制御する。
bool i2cReadReg8(uint8_t devAddr, uint8_t reg, uint8_t* value);

// devAddrのregから始まるlenバイトを連続して読み出す(1回のトランザクション)。
// 自動インクリメント付きのレジスタ指定が必要なデバイスは、reg側にそのビットを含めて渡す。
bool i2cReadRegs(uint8_t devAddr, uint8_t reg, uint8_t* buf, size_t len);
