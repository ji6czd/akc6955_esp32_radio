// モジュールのログ設定はlog_config.hのincludeより前に定義する。
#define LOG_MODULE_LEVEL LOG_LEVEL_MAIN
#define LOG_MODULE_TAG "AK6955"
#include "akc6955_mode.h"

#include <Arduino.h>

#include "es8388.h"
#include "freq_store.h"
#include "i2c_manager.h"
#include "log_config.h"

namespace {

constexpr uint8_t kAk6955Addr = 0x10;
constexpr int kPowerOnPin = 23;  // P_ON。HIGHで電源ON

// ---- レジスタ定義 (AKC6955 データシート V2.3 7章) ----
// Reg0-19は読み書き可、Reg20-27は読み出し専用。Reg14-19はチップ予約なので触らない。
// 「rsv」ビットは書き換えず、必ず読み出してから変更ビットだけ更新すること。

// Reg0 (既定0x4C): 動作モードと選局トリガ
constexpr uint8_t kRegControl =
    0x00;  // power_on[7] fm_en[6] tune[5] seek[4] seekup[3] mute[2]
constexpr uint8_t kCtlPowerOn = 0x80;  // 1:動作 0:停止
constexpr uint8_t kCtlFmEn = 0x40;     // 1:FM 0:AM
constexpr uint8_t kCtlTune = 0x20;     // 0->1でtune開始 (完了はSTC)
constexpr uint8_t kCtlSeek = 0x10;     // 0->1でseek開始 (0に戻すと中断)
constexpr uint8_t kCtlSeekUp = 0x08;   // 1:チャンネル番号増加方向
constexpr uint8_t kCtlMute = 0x04;     // 1:L/Rミュート

// Reg1 (既定0x10): バンド選択。バンドはseek範囲のみ有効で、tuneは任意周波数OK
constexpr uint8_t kRegBand = 0x01;  // amband[7:3] fmband[2:0]
constexpr uint8_t kAmBandShift = 3;
constexpr uint8_t kAmBandMask = 0xF8;
constexpr uint8_t kFmBandMask = 0x07;
// amband: 0:LW(3k) 1:MW1(5k) 2:MW2(9k,0.522-1.62MHz) 3:MW3(10k)
//         4-15:SW1-SW12 16:SW13(11.4-17.9MHz) 18:MW4(0.52-1.73MHz)
//         その他:ユーザー定義(間隔はmode3kで3K/5K)
constexpr uint8_t kAmBandMw2 = 2;
constexpr uint8_t kAmBandUser =
    31;  // ユーザー定義(プリセット以外の値ならどれでもよい)
// fmband: 0:FM1(87-108) 1:FM2(76-108) 2:FM3(70-93) 3:FM4(76-90) 4:FM5(64-88)
//         5:TV1(56.25-91.75) 6:TV2(174.75-222.25) 7:ユーザー定義FM
constexpr uint8_t kFmBandFm1 = 0;
constexpr uint8_t kFmBandFm2 = 1;
constexpr uint8_t kFmBandUser = 7;

// Reg2 (既定0x4A) / Reg3 (既定0xC8): チャンネル番号13bit
//   FM: freq = 25kHz * chan + 30MHz / AM: 5kHz * chan (mode3k=1なら3kHz * chan)
//   MW2(9k)では3の倍数のchanを書くこと(でないと同調が乱れる)
constexpr uint8_t kRegChanHigh = 0x02;  // ref_32kmode[6] mode3k[5] chan[12:8]
constexpr uint8_t kRegChanLow = 0x03;   // chan[7:0]
constexpr uint8_t kRef32kMode = 0x40;   // 1:基準クロック32.768kHz 0:12MHz
constexpr uint8_t kMode3k = 0x20;       // AM: チャンネル間隔 3kHz (0なら5kHz)
constexpr uint8_t kChanHighMask = 0x1F;

// Reg4/Reg5: ユーザー定義バンドの範囲。chan = 32 * 設定値
constexpr uint8_t kRegUserChanStart = 0x04;  // 既定0x19 (FM 50MHz)
constexpr uint8_t kRegUserChanStop = 0x05;   // 既定0x32 (FM 70MHz)

// Reg6 (既定0xA1): 音量。pd_adc_vol=1のときレジスタ音量が有効
constexpr uint8_t kRegVolume = 0x06;  // volume[7:2] line[1] phase_inv[0]
constexpr uint8_t kVolumeShift =
    2;  // 0-23:ミュート 24(最小)-63(最大) 1.5dB/step
constexpr uint8_t kVolumeMin = 24;
constexpr uint8_t kVolumeMax = 63;
constexpr uint8_t kVolLine = 0x02;  // 1:ライン入力モード
constexpr uint8_t kVolPhaseInv =
    0x01;  // 0:同相(スピーカ2個) 1:逆相(スピーカ1個)

// Reg7 (既定0xA1): FM音質設定
constexpr uint8_t kRegAudio =
    0x07;  // bit7 rsv(通常0) bit6 rsv de bben stereo mono bw[1:0]
constexpr uint8_t kAudioDe50us = 0x20;  // 0:75us(米) 1:50us(日本は50us)
constexpr uint8_t kAudioBassBoost = 0x10;
constexpr uint8_t kAudioStereo =
    0x08;  // {stereo,mono}: 00自動 10導頻でステレオ固定 x1モノ固定
constexpr uint8_t kAudioMono = 0x04;
constexpr uint8_t kAudioBwMask = 0x03;  // FM帯域 00:150k 01:200k 10:50k 11:100k

// Reg8 (既定0x58): 判台しきい値
constexpr uint8_t kRegThreshold = 0x08;
// fm_cnr_th[7:6] 00:2dB 01:3dB 10:4dB 11:5dB
// am_cnr_th[5:4] 00:6/3dB 01:10/7dB 10:14/11dB 11:18/15dB (MW,LW/SW)
// fd_th[3:2]     周波数偏差 00:12k 01:24k 10:36k 11:48k (FM)
// stereo_th[1:0] ステレオ開始CNR 00:4 01:8 10:12 11:16

// Reg9 (既定0x07): 音量方式・クロック源
constexpr uint8_t kRegPower2 = 0x09;  // bit7-4 rsv, 他は下記
constexpr uint8_t kPdAdcVol = 0x08;  // 0:ボリューム(可変抵抗) 1:I2Cレジスタ音量
constexpr uint8_t kOscEn = 0x04;     // 0:外部クロック 1:水晶
constexpr uint8_t kLvEn = 0x01;      // 低電圧動作モード許可
// Reg10は予約(0x7F)。触らない

// Reg11 (既定0xE0): FM seek間隔
constexpr uint8_t kRegSeekStep = 0x0B;  // space[5:4]
constexpr uint8_t kSeekStepMask = 0x30;
constexpr uint8_t kSeekStepShift = 4;  // 0:25k 1:50k 2:100k 3:200k(TVは100k)

// Reg12 (既定0x00): 省電力。ライン入力時はpd_rx/pd_adcを1にして消費電力を抑える
constexpr uint8_t kRegPowerDown = 0x0C;
constexpr uint8_t kPdAdc = 0x80;  // 1:信号ADC停止
constexpr uint8_t kPdRx = 0x20;   // 1:アナログ/RFチャネル停止

// Reg13 (既定0x00): TUNDピンと出力音量補正
constexpr uint8_t kRegMisc = 0x0D;
constexpr uint8_t kStLed = 0x40;  // 1:FMでTUNDがステレオ表示灯 0:同調表示灯
constexpr uint8_t kVolPreMask =
    0x0C;  // vol_pre 00:0dB 01:3.5dB 10:7dB 11:10.5dB
constexpr uint8_t kVolPreShift = 2;

// Reg14-19: チップ予約 (0x40,0x82,0xCC,0xFD,0x80,0x80)。書き換え禁止

// ---- 読み出し専用レジスタ (20-27) ----
constexpr uint8_t kRegStatus = 0x14;   // ST[7] STC[6] Tuned[5] readchan[12:8]
constexpr uint8_t kStatStereo = 0x80;  // 1:FMステレオ受信中
constexpr uint8_t kStatStc = 0x40;     // 1:tune/seek完了 (処理中は0)
constexpr uint8_t kStatTuned = 0x20;   // 1:放送局あり
constexpr uint8_t kRegReadChanLow = 0x15;  // readchan[7:0]
constexpr uint8_t kRegCnrAm = 0x16;        // mode3k_f[7] cnram[6:0] (dB)
constexpr uint8_t kMode3kFlag = 0x80;      // 1:AMチャンネル間隔3k
constexpr uint8_t kRegCnrFm = 0x17;        // st_dem[7] cnrfm[6:0] (dB)
constexpr uint8_t kStDemodFlag = 0x80;     // ステレオ比率30%超で1
constexpr uint8_t kRegPgaLevel =
    0x18;  // pgalevel_rf[7:5] pgalevel_if[4:2] lvmode[0]
constexpr uint8_t kRegVbat = 0x19;  // vbat[5:0]: V = 1.8 + 0.05 * vbat
constexpr uint8_t kRegFdNum =
    0x1A;  // 周波数偏差(符号付き8bit) FM:1kHz AM:100Hz単位
constexpr uint8_t kRegRssi = 0x1B;  // rssi[6:0]
// 入力レベル(dBuV) = 103(FM/SW) または 123(MW/LW) - rssi
//                    - 6*pgalevel_rf - 6*pgalevel_if
constexpr uint8_t kLastReg = 0x1D;  // 連続書き込みの最終アドレス

// P_ONをHIGHにしてからI2Cに応答するまでの待ち。実機では5msで立ち上がった。
constexpr uint32_t kPowerOnDelayMs = 5;
// 電源投入(power_on=1)後、tuneが受け付けられるようになるまでの待ち。
// 実測では最初のtuneのSTCが約1.3秒間1のままだった(データシートの「開機時間1s」)ので、
// 余裕を見て1.5秒待つ。
constexpr uint32_t kPowerUpMs = 1500;
// tune完了(STC=1)の待ち上限。実測では約38msなので余裕を見る。
constexpr uint32_t kTuneTimeoutMs = 200;

// Reg2には基準クロック選択(ref_32kmode)とチャンネル上位も入っているので、
// 読み出して mode3k ビットだけを書き換える。
bool setMode3k(bool enabled) {
  uint8_t v = 0;
  if (!i2cReadReg8(kAk6955Addr, kRegChanHigh, &v)) return false;
  v = enabled ? (v | kMode3k) : (v & ~kMode3k);
  return i2cWriteReg8(kAk6955Addr, kRegChanHigh, v);
}

// レジスタの一部のビットだけを書き換える(rsvビットを保つため読み出してから書く)。
bool updateReg(uint8_t reg, uint8_t mask, uint8_t value) {
  uint8_t v = 0;
  if (!i2cReadReg8(kAk6955Addr, reg, &v)) return false;
  v = (v & ~mask) | (value & mask);
  return i2cWriteReg8(kAk6955Addr, reg, v);
}

// 電源を入れる。P_ONピンをHIGHにして5ms待ち(I2Cが応答する)、power_on=1・ミュートONにした
// あと、チップが使えるようになるまで約1.5秒待つ(この間このタスクはブロックされる)。
// ミュートは解除しない。選局が終わってからak6955Unmute()で解除すること。
// 周波数・バンド・音量はチップの既定値のまま(FM1, 87MHz付近)。
bool ak6955PowerOn() {
  pinMode(kPowerOnPin, OUTPUT);
  digitalWrite(kPowerOnPin, HIGH);
  delay(kPowerOnDelayMs);
  if (!updateReg(kRegControl, kCtlPowerOn | kCtlMute, kCtlPowerOn | kCtlMute)) {
    return false;
  }
  delay(kPowerUpMs);
  return true;
}

bool ak6955Unmute() { return updateReg(kRegControl, kCtlMute, 0); }

void ak6955PowerOff() {
  updateReg(kRegControl, kCtlPowerOn, 0);  // power_on=0
  digitalWrite(kPowerOnPin, LOW);
}

// tune/seekの完了(STC=1)を待つ。tune/seek起動直後に呼ぶこと。
// データシートでは「tune/seek中はSTC=0」で、実測でも電源が入って安定していれば
// 起動の約0.5ms後にSTC=0、約38ms後にSTC=1に戻る。起動直後(呼ばれた時点)にSTC=1の
// ままなら想定外(電源投入直後の待ち不足など)としてfalseを返す。
// STC=1にならないままタイムアウトしてもfalse。falseのときは理由をログに出す。
bool waitTuneDone(uint32_t timeoutMs) {
  const uint32_t start = millis();
  uint8_t st = 0;
  if (!i2cReadReg8(kAk6955Addr, kRegStatus, &st)) {
    LOGE("AK6955 tune: status read failed");
    return false;
  }
  if (st & kStatStc) {
    LOGE("AK6955 tune: STC already 1 right after start (reg20=0x%02X)", st);
    return false;
  }
  do {
    delay(2);
    if (!i2cReadReg8(kAk6955Addr, kRegStatus, &st)) {
      LOGE("AK6955 tune: status read failed");
      return false;
    }
    if (st & kStatStc) {
      LOGD("AK6955 tune done in %lu ms",
           static_cast<unsigned long>(millis() - start));
      return true;
    }
  } while (millis() - start < timeoutMs);
  LOGE("AK6955 tune: timeout waiting STC=1 (reg20=0x%02X)", st);
  return false;
}

// AM/FMを切り替える。fm=trueでFM、falseでAM。
// fm_enの変更はtuneを実行して初めて反映されるので、tune(0->1)まで行う。
// power_on・mute・seekupなどReg0の他のビットは読み出して保つ。
// バンド・チャンネル番号・mode3kは設定しない(AMとFMでチャンネル番号の意味が変わるため、
// 呼び出し側でバンドと周波数を続けて設定すること)。
bool setFmMode(bool fm) {
  uint8_t ctl = 0;
  if (!i2cReadReg8(kAk6955Addr, kRegControl, &ctl)) return false;
  ctl = fm ? (ctl | kCtlFmEn) : (ctl & ~kCtlFmEn);
  ctl &= ~kCtlTune;  // 0->1の立ち上がりを作るため一度tune=0にする
  if (!i2cWriteReg8(kAk6955Addr, kRegControl, ctl)) return false;
  if (!i2cWriteReg8(kAk6955Addr, kRegControl, ctl | kCtlTune)) return false;
  return waitTuneDone(kTuneTimeoutMs);
}

// チャンネル番号(13bit)を書く。Reg2にはmode3kなども入っているので上位5bitだけ更新する。
// 反映にはtuneが必要(setFmModeが行う)。
bool setChannel(uint16_t chan) {
  if (!updateReg(kRegChanHigh, kChanHighMask, (chan >> 8) & kChanHighMask)) {
    return false;
  }
  return i2cWriteReg8(kAk6955Addr, kRegChanLow, chan & 0xFF);
}

// ---- バンド定義 (ロータリー位置 = テーブルの添字) ----
// AMはユーザー定義バンド。バンドの範囲(Reg4/5)が効くのはseekだけで、tune(手動選局)は
// 任意の周波数にできる。チャンネル番号の単位は AM: mode3kで3kHz/5kHz、FM:
// 25kHz。
struct Band {
  bool fm;
  uint8_t bandBits;  // AM: amband / FM: fmband
  bool mode3k;       // AMのみ。true=3kHz刻み false=5kHz刻み
  uint32_t minKhz;
  uint32_t maxKhz;
  uint32_t initialKhz;
};

constexpr Band kBands[] = {
    {false, kAmBandUser, true, 150, 1710, 594},       // LW〜MW, 3kHz刻み
    {false, kAmBandUser, false, 1710, 30000, 7000},   // MW上端〜SW, 5kHz刻み
    {true, kFmBandFm2, false, 76000, 108000, 80000},  // FM (FM2: 76-108MHz)
};
constexpr int kBandCount = sizeof(kBands) / sizeof(kBands[0]);

// 周波数(kHz)→チャンネル番号(最も近いチャンネルへ丸める)。
// FM: (kHz-30000)/25、AM: kHz/(mode3k?3:5)。
constexpr uint16_t khzToChan(const Band& b, uint32_t khz) {
  return static_cast<uint16_t>(b.fm ? (khz - 30000 + 12) / 25
                                    : (khz + (b.mode3k ? 1 : 2)) /
                                          (b.mode3k ? 3 : 5));
}

// テンキーで入力できる最大桁数(kHz単位)。バンドの上限周波数の桁数にする
// (position 0: 1710kHz→4桁、position 1: 30000kHz→5桁、FM: 108000kHz→6桁)。
constexpr int maxDigits(const Band& b) {
  int digits = 1;
  for (uint32_t v = b.maxKhz; v >= 10; v /= 10) ++digits;
  return digits;
}

// キーが数字キーなら0〜9を、そうでなければ-1を返す。
int digitOfKey(uint8_t id) {
  switch (static_cast<PanelKey>(id)) {
    case PanelKey::kTact0:
      return 0;
    case PanelKey::kTact1:
      return 1;
    case PanelKey::kTact2:
      return 2;
    case PanelKey::kTact3:
      return 3;
    case PanelKey::kTact4:
      return 4;
    case PanelKey::kTact5:
      return 5;
    case PanelKey::kTact6:
      return 6;
    case PanelKey::kTact7:
      return 7;
    case PanelKey::kTact8:
      return 8;
    case PanelKey::kTact9:
      return 9;
    default:
      return -1;
  }
}

// バンドを選んで周波数khzに同調する。
// バンド・チャンネルを先に書いておき、最後のsetFmModeのtuneでまとめて反映させる。
bool selectBand(const Band& b, uint32_t khz) {
  bool ok;
  if (b.fm) {
    ok = updateReg(kRegBand, kFmBandMask, b.bandBits);
  } else {
    // ユーザー定義バンドの範囲(seek用)。chan = 32 * 設定値。
    const uint16_t lo = khzToChan(b, b.minKhz);
    const uint16_t hi = khzToChan(b, b.maxKhz);
    ok = updateReg(kRegBand, kAmBandMask, b.bandBits << kAmBandShift) &&
         setMode3k(b.mode3k) &&
         i2cWriteReg8(kAk6955Addr, kRegUserChanStart, lo / 32) &&
         i2cWriteReg8(kAk6955Addr, kRegUserChanStop, (hi + 31) / 32);
  }
  return ok && setChannel(khzToChan(b, khz)) && setFmMode(b.fm);
}

// 現在のチャンネル番号(13bit)を読む。Reg20/21の読み出し専用レジスタを使う
// (Reg2/3は書き込み値。tune後の実際のチャンネルとは限らない)。
// tune/seek中(STC=0)は値が確定しないので、完了後に呼ぶこと。
bool readChannel(uint16_t* chan) {
  uint8_t hi = 0, lo = 0;
  if (!i2cReadReg8(kAk6955Addr, kRegStatus, &hi)) return false;
  if (!i2cReadReg8(kAk6955Addr, kRegReadChanLow, &lo)) return false;
  *chan = static_cast<uint16_t>(((hi & kChanHighMask) << 8) | lo);
  return true;
}

// 現在の周波数をkHzで読む。FM: 25kHz*chan+30MHz、AM:
// 5kHz*chan(mode3k_f=1なら3kHz*chan)。
bool readFreqKhz(uint32_t* khz) {
  uint8_t ctl = 0, am = 0;
  uint16_t chan = 0;
  if (!i2cReadReg8(kAk6955Addr, kRegControl, &ctl)) return false;
  if (!i2cReadReg8(kAk6955Addr, kRegCnrAm, &am)) return false;
  if (!readChannel(&chan)) return false;
  if (ctl & kCtlFmEn) {
    *khz = 30000u + 25u * chan;
  } else {
    *khz = ((am & kMode3kFlag) ? 3u : 5u) * chan;
  }
  return true;
}

// 現在の局からdeltaChanチャンネル(符号付き)動かして選局する。
// バンドの範囲を超えたら反対の端へ回る。
bool tuneBy(const Band& b, int deltaChan) {
  uint16_t chan = 0;
  if (!readChannel(&chan)) return false;
  const int lo = khzToChan(b, b.minKhz);
  const int hi = khzToChan(b, b.maxKhz);
  const int span = hi - lo + 1;
  const int offset = ((chan + deltaChan - lo) % span + span) % span;
  if (!setChannel(static_cast<uint16_t>(lo + offset))) return false;
  return setFmMode(b.fm);  // tuneして反映・完了待ち
}

// 周波数khz(kHz)に直接同調する。範囲外はfalse(同調しない)。
bool tuneToKhz(const Band& b, uint32_t khz) {
  if (khz < b.minKhz || khz > b.maxKhz) return false;
  if (!setChannel(khzToChan(b, khz))) return false;
  return setFmMode(b.fm);
}

// エンコーダの回転がこの間隔(ms)より速いときは、1ディテントで動かす量を増やす。
constexpr uint32_t kFastRotateMs = 60;
constexpr int kFastMultiplier = 10;
// 電源を入れて、ロータリー位置に応じたバンドを選局する。
// エンコーダ(回転)とエンコーダ軸の+/-キーで周波数を変える。
class Ak6955Mode : public Mode {
 public:
  const char* name() const override { return "ak6955"; }

  // 音量ではなく周波数を変えるため、エンコーダの回転をこのモードで受ける。
  bool handlesEncoder() const override { return true; }

  // variant: ロータリー位置。kBandsの添字。
  void enter(int variant) override {
    m_ready = false;
    m_pendingChan = 0;
    m_entry = 0;
    m_entryDigits = 0;
    m_targetKhz = 0;
    m_band = (variant >= 0 && variant < kBandCount) ? variant : 0;
    const Band& b = kBands[m_band];
    i2cManagerBegin();
    es8388SetLineInEnabled(true);
    if (!ak6955PowerOn()) {
      LOGE("AK6955 power on failed (I2C)");
      return;
    }
    LOGI("power on");
    // 前回このバンドで聴いていた周波数があれば、それに同調する(範囲外なら初期局)。
    uint32_t khz = freqStoreLoad(m_band, b.initialKhz);
    if (khz < b.minKhz || khz > b.maxKhz) khz = b.initialKhz;
    if (!selectBand(b, khz)) {
      LOGE("AK6955 setup failed (band %d)", m_band);
      return;
    }
    if (!ak6955Unmute()) {
      LOGE("AK6955 unmute failed (I2C)");
      return;
    }
    m_ready = true;
    logStation();
  }

  void exit() override {
    // 電源を切る前に、現在の周波数をこのバンド用に保存する。
    if (m_ready) {
      uint32_t khz = 0;
      if (readFreqKhz(&khz)) {
        freqStoreNotifyChanged(m_band, khz);
      } else {
        LOGE("AK6955 channel read failed (not saved)");
      }
    }
    m_ready = false;
    ak6955PowerOff();
    es8388SetLineInEnabled(false);
    LOGI("power off");
  }

  // エンコーダの回転とエンコーダ軸の+/-キーを周波数の増減として溜める。
  // VOICEキーで現在のチャンネル番号をログに出す(いずれユーザへ音声で伝える予定)。
  void onEvent(const PanelEvent& ev) override {
    if (!m_ready) return;
    if (ev.type == PanelEventType::kEncoderStep) {
      addSteps(ev.value, true);
    } else if (ev.type == PanelEventType::kKeyDown) {
      if (ev.id == static_cast<uint8_t>(PanelKey::kEncoderPlus)) {
        addSteps(1, false);
      } else if (ev.id == static_cast<uint8_t>(PanelKey::kEncoderMinus)) {
        addSteps(-1, false);
      } else if (ev.id == static_cast<uint8_t>(PanelKey::kVoice)) {
        logStation();
      } else if (ev.id == static_cast<uint8_t>(PanelKey::kTactClear)) {
        clearEntry();
      } else if (ev.id == static_cast<uint8_t>(PanelKey::kTactEnter)) {
        commitEntry();
      } else {
        const int digit = digitOfKey(ev.id);
        if (digit >= 0) addDigit(digit);
      }
    }
  }

  // 溜めた分をまとめて1回のtuneで選局する(tuneは約40msかかるので、回転が速いときに
  // イベントごとに選局しない)。
  void tick() override {
    if (!m_ready) return;
    if (m_targetKhz != 0) {  // テンキー入力の確定が優先。回転で溜めた分は捨てる
      const uint32_t khz = m_targetKhz;
      m_targetKhz = 0;
      m_pendingChan = 0;
      if (!tuneToKhz(kBands[m_band], khz)) {
        LOGE("AK6955 tune to %lu kHz failed", static_cast<unsigned long>(khz));
        return;
      }
    } else if (m_pendingChan != 0) {
      const int delta = m_pendingChan;
      m_pendingChan = 0;
      if (!tuneBy(kBands[m_band], delta)) {
        LOGE("AK6955 tune failed");
        return;
      }
    } else {
      return;
    }
    logStation();
    // 選局のたびに保存を予約する(実際の書き込みは選局が止まってから)。
    uint32_t khz = 0;
    if (readFreqKhz(&khz)) freqStoreNotifyChanged(m_band, khz);
  }

 private:
  // 数字キー:
  // 周波数をkHzで1桁ずつ入力する(桁数の上限はバンドの上限周波数の桁数)。
  void addDigit(int digit) {
    const int limit = maxDigits(kBands[m_band]);
    if (m_entryDigits >= limit) {
      LOGW("entry full (%d digits)", limit);
      return;
    }
    m_entry = m_entry * 10 + digit;
    ++m_entryDigits;
    LOGI("entry: %lu", static_cast<unsigned long>(m_entry));
  }

  void clearEntry() {
    if (m_entryDigits > 0) LOGI("entry cleared");
    m_entry = 0;
    m_entryDigits = 0;
  }

  // Enterで確定。バンドの範囲外なら同調せずに入力を捨てる。
  void commitEntry() {
    if (m_entryDigits == 0) return;
    const Band& b = kBands[m_band];
    const uint32_t khz = m_entry;
    clearEntry();
    if (khz < b.minKhz || khz > b.maxKhz) {
      LOGW("%lu kHz is out of range (%lu-%lu kHz)",
           static_cast<unsigned long>(khz),
           static_cast<unsigned long>(b.minKhz),
           static_cast<unsigned long>(b.maxKhz));
      return;
    }
    m_targetKhz = khz;
  }

  void addSteps(int steps, bool accelerate) {
    int multiplier = 1;
    if (accelerate) {
      const uint32_t now = millis();
      if (now - m_lastRotateMs < kFastRotateMs) multiplier = kFastMultiplier;
      m_lastRotateMs = now;
    }
    m_pendingChan += steps * multiplier;
  }

  void logStation() {
    uint16_t chan = 0;
    uint32_t khz = 0;
    if (readChannel(&chan) && readFreqKhz(&khz)) {
      LOGI("band %d: chan=%u freq=%lu kHz", m_band, chan,
           static_cast<unsigned long>(khz));
    } else {
      LOGE("AK6955 channel read failed");
    }
  }

  bool m_ready = false;
  int m_band = 0;
  int m_pendingChan = 0;
  uint32_t m_lastRotateMs = 0;
  uint32_t m_entry = 0;      // テンキーで入力中の数値(kHz)
  int m_entryDigits = 0;     // 入力済みの桁数
  uint32_t m_targetKhz = 0;  // Enterで確定した周波数。0なら無し(tickで同調する)
};

}  // namespace

Mode& ak6955Mode() {
  static Ak6955Mode mode;
  return mode;
}
