// モジュールのログ設定はlog_config.hのincludeより前に定義する。
#define LOG_MODULE_LEVEL LOG_LEVEL_TS
#define LOG_MODULE_TAG "TS"
#include "ts_demux.h"

#include <cstring>

#include "log_config.h"

namespace {

constexpr int kPidPat = 0x0000;
constexpr uint8_t kStreamTypeAdtsAac = 0x0F;
constexpr uint8_t kStreamTypeLoasAac = 0x11;

}  // namespace

void TsDemuxer::feed(const uint8_t* data, size_t len) {
  size_t pos = 0;

  if (carryLen_ > 0) {
    size_t want = kPacketSize - carryLen_;
    size_t take = len < want ? len : want;
    memcpy(carry_ + carryLen_, data, take);
    carryLen_ += take;
    pos += take;
    if (carryLen_ < kPacketSize) {
      return;
    }
    handlePacket(carry_);
    carryLen_ = 0;
  }

  while (pos + kPacketSize <= len) {
    handlePacket(data + pos);
    pos += kPacketSize;
  }

  size_t remain = len - pos;
  if (remain > 0) {
    memcpy(carry_, data + pos, remain);
    carryLen_ = remain;
  }
}

void TsDemuxer::handlePacket(const uint8_t* pkt) {
  if (pkt[0] != 0x47) {
    LOGW("lost sync (got 0x%02X instead of 0x47)", pkt[0]);
    return;
  }

  bool pusi = (pkt[1] & 0x40) != 0;
  int pid = ((pkt[1] & 0x1F) << 8) | pkt[2];
  uint8_t adaptationFieldControl = (pkt[3] >> 4) & 0x3;

  // adaptationFieldControl: 00=予約, 01=payloadのみ, 10=adaptation fieldのみ,
  // 11=adaptation field+payload。00/10はこのモジュールが使うPSI/PESには現れない。
  if (adaptationFieldControl == 0 || adaptationFieldControl == 2) {
    return;
  }

  const uint8_t* payload = pkt + 4;
  size_t payloadLen = kPacketSize - 4;
  if (adaptationFieldControl == 3) {
    uint8_t adaptationLen = payload[0];
    size_t skip = 1u + adaptationLen;
    if (skip > payloadLen) {
      return;
    }
    payload += skip;
    payloadLen -= skip;
  }
  if (payloadLen == 0) {
    return;
  }

  if (pid == kPidPat) {
    parsePat(payload, payloadLen, pusi);
  } else if (pmtPid_ >= 0 && pid == pmtPid_) {
    parsePmt(payload, payloadLen, pusi);
  } else if (audioPid_ >= 0 && pid == audioPid_) {
    parseAudioPes(payload, payloadLen, pusi);
  }
}

void TsDemuxer::parsePat(const uint8_t* p, size_t len, bool pusi) {
  // PAT/PMTは1TSパケットに収まる前提(このアプリが対象とする配信で実測済み)。
  // pointer_fieldを跨いだセクション再組み立てはしない。
  if (!pusi || len < 1) {
    return;
  }
  uint8_t pointerField = p[0];
  if (1u + pointerField >= len) {
    return;
  }
  p += 1 + pointerField;
  len -= 1 + pointerField;
  if (len < 8) {
    return;
  }

  size_t sectionLen = ((p[1] & 0x0F) << 8) | p[2];
  if (sectionLen < 9 || 3 + sectionLen > len) {
    return;
  }
  const uint8_t* body = p + 3;
  size_t bodyLen = sectionLen - 4;  // 末尾のCRC32を除く

  // body: transport_stream_id(2) version/current_next(1) section_number(1)
  //       last_section_number(1) [program_number(2) PID(2)]* CRC32(4)
  size_t off = 5;
  while (off + 4 <= bodyLen) {
    uint16_t programNumber = (body[off] << 8) | body[off + 1];
    int candidatePid = ((body[off + 2] & 0x1F) << 8) | body[off + 3];
    if (programNumber != 0 && pmtPid_ < 0) {
      pmtPid_ = candidatePid;
      LOGD("PMT pid=0x%04X (program %u)", pmtPid_, programNumber);
    }
    off += 4;
  }
}

void TsDemuxer::parsePmt(const uint8_t* p, size_t len, bool pusi) {
  if (!pusi || len < 1) {
    return;
  }
  uint8_t pointerField = p[0];
  if (1u + pointerField >= len) {
    return;
  }
  p += 1 + pointerField;
  len -= 1 + pointerField;
  if (len < 12) {
    return;
  }

  size_t sectionLen = ((p[1] & 0x0F) << 8) | p[2];
  if (sectionLen < 9 || 3 + sectionLen > len) {
    return;
  }
  const uint8_t* body = p + 3;
  size_t bodyLen = sectionLen - 4;  // 末尾のCRC32を除く
  if (bodyLen < 9) {
    return;
  }

  // body: program_number(2) version/current_next(1) section_number(1)
  //       last_section_number(1) PCR_PID(2) program_info_length(2)
  //       [descriptor]* [stream_type(1) elementary_PID(2) ES_info_length(2) [descriptor]*]* CRC32(4)
  size_t programInfoLen = ((body[7] & 0x0F) << 8) | body[8];
  size_t off = 9 + programInfoLen;
  while (off + 5 <= bodyLen) {
    uint8_t streamType = body[off];
    int elementaryPid = ((body[off + 1] & 0x1F) << 8) | body[off + 2];
    size_t esInfoLen = ((body[off + 3] & 0x0F) << 8) | body[off + 4];
    if ((streamType == kStreamTypeAdtsAac || streamType == kStreamTypeLoasAac) && audioPid_ < 0) {
      audioPid_ = elementaryPid;
      LOGI("audio pid=0x%04X (stream_type=0x%02X)", audioPid_, streamType);
    }
    off += 5 + esInfoLen;
  }
}

void TsDemuxer::parseAudioPes(const uint8_t* p, size_t len, bool pusi) {
  if (!pusi) {
    // PESヘッダを持たない継続パケット: そのままESの続き。
    onAudio_(p, len);
    return;
  }
  if (len < 9 || p[0] != 0x00 || p[1] != 0x00 || p[2] != 0x01) {
    LOGW("bad PES start code");
    return;
  }
  // p[3]=stream_id, p[4..5]=PES_packet_length, p[6..7]=フラグ, p[8]=PES_header_data_length
  uint8_t headerDataLen = p[8];
  size_t esOffset = 9 + headerDataLen;
  if (esOffset >= len) {
    return;
  }
  onAudio_(p + esOffset, len - esOffset);
}
