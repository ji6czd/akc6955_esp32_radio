// モジュールのログ設定はlog_config.hのincludeより前に定義する。
#define LOG_MODULE_LEVEL LOG_LEVEL_HW
#define LOG_MODULE_TAG "HW"
#include "tca6424a.h"

#include <Arduino.h>

#include "i2c_manager.h"
#include "log_config.h"

namespace {

constexpr uint8_t kAddr[kTca6424aDeviceCount] = {0x22, 0x23};

// コマンドバイト: bit7=AI(自動インクリメント), bit3-0=レジスタ。ポートは3つ連続している。
constexpr uint8_t kAutoIncrement = 0x80;
constexpr uint8_t kRegInput0 = 0x00;     // 入力ポート0-2
constexpr uint8_t kRegPolarity0 = 0x08;  // 極性反転ポート0-2(1=入力レベルを反転して読む)
constexpr uint8_t kRegConfig0 = 0x0C;    // 入出力設定ポート0-2(1=入力)
constexpr uint8_t kPortCount = 3;

bool writePorts(uint8_t addr, uint8_t reg, uint8_t value) {
  for (uint8_t port = 0; port < kPortCount; ++port) {
    if (!i2cWriteReg8(addr, reg + port, value)) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool tca6424aBegin() {
  i2cManagerBegin();

  bool allOk = true;
  for (int i = 0; i < kTca6424aDeviceCount; ++i) {
    // 入力設定はリセット後の既定値と同じだが、ESP32だけがリセットされた場合に備えて明示する。
    // 極性反転は全ピンオン: スイッチはGNDへ落ちる配線なので、押されている/選択中を1として読む。
    bool ok = writePorts(kAddr[i], kRegConfig0, 0xFF) && writePorts(kAddr[i], kRegPolarity0, 0xFF);
    if (!ok) {
      LOGE("TCA6424A #%d (0x%02X) not responding", i, kAddr[i]);
      allOk = false;
    }
  }
  return allOk;
}

bool tca6424aRead(const int device, uint32_t& bits) {
  if (device < 0 || device >= kTca6424aDeviceCount) {
    return false;
  }
  uint8_t buf[kPortCount];
  if (!i2cReadRegs(kAddr[device], kAutoIncrement | kRegInput0, buf, kPortCount)) {
    return false;
  }
  bits = static_cast<uint32_t>(buf[0]) | (static_cast<uint32_t>(buf[1]) << 8) | (static_cast<uint32_t>(buf[2]) << 16);
  return true;
}
