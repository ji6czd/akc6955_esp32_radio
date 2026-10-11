// モジュールのログ設定はlog_config.hのincludeより前に定義する。
#define LOG_MODULE_LEVEL LOG_LEVEL_HW
#define LOG_MODULE_TAG "HW"
#include "es8388.h"

#include <Arduino.h>

#include "i2c_manager.h"
#include "log_config.h"

namespace {

constexpr uint8_t kEs8388Addr = 0x11;
constexpr int kAmpMutePin = 32;

constexpr uint8_t kRegControl1 = 0x00;
constexpr uint8_t kRegControl2 = 0x01;
constexpr uint8_t kRegChipPower = 0x02;
constexpr uint8_t kRegAdcPower = 0x03;
constexpr uint8_t kRegDacPower = 0x04;
constexpr uint8_t kRegMasterMode = 0x08;
constexpr uint8_t kRegDacControl1 = 0x17;
constexpr uint8_t kRegDacControl2 = 0x18;
constexpr uint8_t kRegDacControl3 = 0x19;
constexpr uint8_t kRegDacControl4 = 0x1A;  // LDACVOL
constexpr uint8_t kRegDacControl5 = 0x1B;  // RDACVOL
constexpr uint8_t kRegDacControl16 = 0x26;
constexpr uint8_t kRegDacControl17 = 0x27;
constexpr uint8_t kRegDacControl18 = 0x28;
constexpr uint8_t kRegDacControl19 = 0x29;
constexpr uint8_t kRegDacControl20 = 0x2A;
constexpr uint8_t kRegDacControl21 = 0x2B;
constexpr uint8_t kRegDacControl24 = 0x2E;  // LOUT1VOL
constexpr uint8_t kRegDacControl25 = 0x2F;  // ROUT1VOL
constexpr uint8_t kRegDacControl26 = 0x30;  // LOUT2VOL
constexpr uint8_t kRegDacControl27 = 0x31;  // ROUT2VOL

constexpr uint8_t kMaxOutVolume = 0x21;  // +4.5dB

uint8_t s_outVolume = 0;

bool writeReg(uint8_t reg, uint8_t value) {
  return i2cWriteReg8(kEs8388Addr, reg, value);
}

}  // namespace

void es8388SetVolume(uint8_t outVolume) {
  if (outVolume > kMaxOutVolume) outVolume = kMaxOutVolume;
  bool ok = true;
  ok = ok && writeReg(kRegDacControl24, outVolume);
  ok = ok && writeReg(kRegDacControl25, outVolume);
  ok = ok && writeReg(kRegDacControl26, outVolume);
  ok = ok && writeReg(kRegDacControl27, outVolume);
  if (ok) {
    s_outVolume = outVolume;
  } else {
    LOGE("ES8388 volume write failed");
  }
}

uint8_t es8388GetVolume() { return s_outVolume; }

void es8388SetLineInEnabled(bool enabled) {
  // DACCONTROL17/20: bit7=DAC->ミキサー, bit6=ライン入力->ミキサー,
  // bit5:3=ライン入力のゲイン (010=0dB,
  // 111=-15dB)。入力の選択はDACCONTROL16(0x26)=0x00でLIN1/RIN1。
  const uint8_t value = enabled ? 0xD0 : 0xB8;
  if (!(writeReg(kRegDacControl16, 0x00) && writeReg(kRegDacControl17, value) &&
        writeReg(kRegDacControl20, value))) {
    LOGE("ES8388 line-in write failed");
  }
}

bool es8388Begin(uint8_t outVolume) {
  i2cManagerBegin();

  // ES8388 User Guide 10.3 "Start up play back mode" の手順どおり。
  bool ok = true;
  ok = ok && writeReg(kRegDacControl3,
                      0x36);  // DACミュート(設定中のポップノイズ対策)
  ok = ok && writeReg(kRegChipPower, 0xF3);   // DEM/STM停止(設定中)
  ok = ok && writeReg(kRegMasterMode, 0x00);  // スレーブモード
  ok = ok && writeReg(kRegDacControl21,
                      0x80);  // DACもADCのLRCKを使う(LRCKは1本のみ配線)
  ok = ok && writeReg(kRegControl1, 0x05);     // Play&Recordモード
  ok = ok && writeReg(kRegControl2, 0x40);     // アナログ回路とIbias起動
  ok = ok && writeReg(kRegAdcPower, 0xFF);     // ADCは使わないので電源断
  ok = ok && writeReg(kRegDacControl2, 0x02);  // MCLK = 256fs
  ok = ok && writeReg(kRegDacControl1, 0x18);  // I2S, 16bit
  ok = ok && writeReg(kRegDacControl24, outVolume);
  ok = ok && writeReg(kRegDacControl25, outVolume);
  ok = ok && writeReg(kRegDacControl26, outVolume);
  ok = ok && writeReg(kRegDacControl27, outVolume);
  ok =
      ok && writeReg(kRegDacPower, 0x3C);  // DAC + LOUT1/ROUT1/LOUT2/ROUT2 有効
  ok = ok && writeReg(kRegDacControl4, 0x00);  // DACデジタルボリューム 0dB
  ok = ok && writeReg(kRegDacControl5, 0x00);
  ok = ok && writeReg(kRegDacControl16, 0x00);
  ok = ok && writeReg(kRegDacControl17, 0xB8);  // LDAC -> Lミキサー
  ok = ok && writeReg(kRegDacControl18, 0x38);
  ok = ok && writeReg(kRegDacControl19, 0x38);
  ok = ok && writeReg(kRegDacControl20, 0xB8);  // RDAC -> Rミキサー
  ok = ok && writeReg(kRegDacControl3, 0x32);   // ミュート解除
  ok = ok && writeReg(kRegChipPower, 0x00);     // DEM/STM起動

  // 診断用:
  // 書き込んだ値が読み戻せるか確認する(I2Cが通っていてもデバイス違いを検出できる)。
  uint8_t v = 0;
  if (i2cReadReg8(kEs8388Addr, kRegControl1, &v)) {
    Serial.printf("ES8388 reg0x00=0x%02X (expect 0x05)\n", v);
  } else {
    Serial.println("ES8388 readback failed");
  }

  if (!ok) {
    LOGE("ES8388 register bring-up failed (I2C NACK?)");
  } else {
    s_outVolume = outVolume;
  }
  return ok;
}

i2s_chan_handle_t es8388CreateI2sTxChannel() {
  constexpr i2s_port_t kI2sPort = I2S_NUM_0;
  constexpr gpio_num_t kMclkPin = GPIO_NUM_0;
  constexpr gpio_num_t kBckPin = GPIO_NUM_5;
  constexpr gpio_num_t kWsPin = GPIO_NUM_25;
  constexpr gpio_num_t kDoutPin = GPIO_NUM_26;
  // reconfig前の仮値。i2s_speaker.cppが最初の実データで実際のレートへreconfigする。
  constexpr uint32_t kInitialSampleRateHz = 48000;

  i2s_chan_handle_t txHandle = nullptr;
  i2s_chan_config_t chanCfg =
      I2S_CHANNEL_DEFAULT_CONFIG(kI2sPort, I2S_ROLE_MASTER);
  if (i2s_new_channel(&chanCfg, &txHandle, nullptr) != ESP_OK) {
    LOGE("I2S new channel failed");
    return nullptr;
  }

  // MCLKはデフォルトの256fs。ES8388のDACCONTROL2(0x18)=0x02と一致させている。
  i2s_std_config_t stdCfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kInitialSampleRateHz),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_STEREO),
      .gpio_cfg =
          {
              .mclk = kMclkPin,
              .bclk = kBckPin,
              .ws = kWsPin,
              .dout = kDoutPin,
              .din = I2S_GPIO_UNUSED,
              .invert_flags = {.mclk_inv = false,
                               .bclk_inv = false,
                               .ws_inv = false},
          },
  };

  if (i2s_channel_init_std_mode(txHandle, &stdCfg) != ESP_OK) {
    LOGE("I2S channel init std mode failed");
    return nullptr;
  }
  return txHandle;
}

void es8388SetAmpEnabled(bool enabled) {
  pinMode(kAmpMutePin, OUTPUT);
  digitalWrite(kAmpMutePin, enabled ? HIGH : LOW);
}
