#pragma once

// このファイルは tools/gen_station_list.py が生成したものです。手で編集しないこと。
// 生成日時: 2026-09-21 08:17:02 JST
// 内容: 412局(NHK 16, radiko 100, JCBA 145, ListenRadio 78, FMぷらぷら 73)
//
// 局IDも配信URLも頻繁には変わらないため、実行時にXMLを取得・パースせず
// この固定テーブルをそのままチャンネル一覧として使う(channel_list.hを参照)。
// 局の増減があったらスクリプトを再実行して差し替える。
//
// 2列目はURLではなく局を指すIDだけ。radikoは局ID(例: "TBS")、NHKはストリーム
// 番号(例: "3")、JCBA/FMぷらぷらは局ID(例: "fmhana")、ListenRadioはChannelId
// (例: "30011")で、実URLは再生開始時に各クライアント(radiko_client.h/
// nhk_client.h/listenradio_client.h、JCBA/FMぷらぷらはselect_stream API)が組み立てる。
//
// 並びはプラットフォーム(ChannelPlatformの列挙順)→エリア(JP番号)→局名。同じ局を
// 複数エリアへ複製しない(NHKは8拠点×AM/FMの16局で、areaは拠点の所在県)。
// 各行末尾のコメントは直接選局コード(プラットフォーム番号1桁 + プラットフォーム内の
// 1始まりの局番号3桁)。kPlatformRanges[] がプラットフォームごとの先頭添字と局数を
// 持ち、channel_list.cpp がコード⇔添字の変換とエリア送り/プラットフォーム送りに使う。
//
// kStationFreeAreaMask[] は各局を無料(非プレミアム)で再生できるエリアの
// ビットマスク。radiko_client.cpp が無料利用時の再生可否判定に使う
// (プレミアム認証時はこの表を見ずに全局を許可する)。NHK/JCBA/ListenRadio行は0。

#include "channel_list.h"

// 文字列はすべてリテラル(フラッシュ常駐)なので、このテーブルはRAMを消費しない。
constexpr Channel kStations[] = {
    // ==== 1: NHK (16局) ====
    // JP1 北海道
    {"NHKAM（札幌）", "6", 1, ChannelPlatform::kNhk},  // 1001
    {"NHKFM（札幌）", "7", 1, ChannelPlatform::kNhk},  // 1002
    // JP4 宮城
    {"NHKAM（仙台）", "8", 4, ChannelPlatform::kNhk},  // 1003
    {"NHKFM（仙台）", "9", 4, ChannelPlatform::kNhk},  // 1004
    // JP13 東京
    {"NHKAM（東京）", "3", 13, ChannelPlatform::kNhk},  // 1005
    {"NHKFM（東京）", "5", 13, ChannelPlatform::kNhk},  // 1006
    // JP23 愛知
    {"NHKAM（名古屋）", "10", 23, ChannelPlatform::kNhk},  // 1007
    {"NHKFM（名古屋）", "11", 23, ChannelPlatform::kNhk},  // 1008
    // JP27 大阪
    {"NHKAM（大阪）", "12", 27, ChannelPlatform::kNhk},  // 1009
    {"NHKFM（大阪）", "13", 27, ChannelPlatform::kNhk},  // 1010
    // JP34 広島
    {"NHKAM（広島）", "14", 34, ChannelPlatform::kNhk},  // 1011
    {"NHKFM（広島）", "15", 34, ChannelPlatform::kNhk},  // 1012
    // JP38 愛媛
    {"NHKAM（松山）", "16", 38, ChannelPlatform::kNhk},  // 1013
    {"NHKFM（松山）", "17", 38, ChannelPlatform::kNhk},  // 1014
    // JP40 福岡
    {"NHKAM（福岡）", "18", 40, ChannelPlatform::kNhk},  // 1015
    {"NHKFM（福岡）", "19", 40, ChannelPlatform::kNhk},  // 1016
    // ==== 2: radiko (100局) ====
    // JP1 北海道
    {"AIR-G'（FM北海道）", "AIR-G", 1, ChannelPlatform::kRadiko},  // 2001
    {"FM NORTH WAVE", "NORTHWAVE", 1, ChannelPlatform::kRadiko},  // 2002
    {"ＨＢＣラジオ", "HBC", 1, ChannelPlatform::kRadiko},  // 2003
    {"ＳＴＶラジオ", "STV", 1, ChannelPlatform::kRadiko},  // 2004
    // JP2 青森
    {"エフエム青森", "AFB", 2, ChannelPlatform::kRadiko},  // 2005
    {"ＲＡＢ青森放送", "RAB", 2, ChannelPlatform::kRadiko},  // 2006
    // JP3 岩手
    {"IBCラジオ", "IBC", 3, ChannelPlatform::kRadiko},  // 2007
    {"エフエム岩手", "FMI", 3, ChannelPlatform::kRadiko},  // 2008
    // JP4 宮城
    {"Date fm エフエム仙台", "DATEFM", 4, ChannelPlatform::kRadiko},  // 2009
    {"tbcラジオ", "TBC", 4, ChannelPlatform::kRadiko},  // 2010
    // JP5 秋田
    {"ABSラジオ", "ABS", 5, ChannelPlatform::kRadiko},  // 2011
    {"エフエム秋田", "AFM", 5, ChannelPlatform::kRadiko},  // 2012
    // JP6 山形
    {"Rhythm Station　エフエム山形", "RFM", 6, ChannelPlatform::kRadiko},  // 2013
    {"YBC山形放送", "YBC", 6, ChannelPlatform::kRadiko},  // 2014
    // JP7 福島
    {"RFCラジオ福島", "RFC", 7, ChannelPlatform::kRadiko},  // 2015
    {"ふくしまFM", "FMF", 7, ChannelPlatform::kRadiko},  // 2016
    // JP8 茨城
    {"LuckyFM 茨城放送", "IBS", 8, ChannelPlatform::kRadiko},  // 2017
    // JP9 栃木
    {"CRT栃木放送", "CRT", 9, ChannelPlatform::kRadiko},  // 2018
    {"RADIO BERRY", "RADIOBERRY", 9, ChannelPlatform::kRadiko},  // 2019
    // JP10 群馬
    {"FM GUNMA", "FMGUNMA", 10, ChannelPlatform::kRadiko},  // 2020
    // JP11 埼玉
    {"NACK5", "NACK5", 11, ChannelPlatform::kRadiko},  // 2021
    // JP12 千葉
    {"BAYFM78", "BAYFM78", 12, ChannelPlatform::kRadiko},  // 2022
    // JP13 東京
    {"J-WAVE", "FMJ", 13, ChannelPlatform::kRadiko},  // 2023
    {"TBSラジオ", "TBS", 13, ChannelPlatform::kRadiko},  // 2024
    {"TOKYO FM", "FMT", 13, ChannelPlatform::kRadiko},  // 2025
    {"interfm", "INT", 13, ChannelPlatform::kRadiko},  // 2026
    {"ニッポン放送", "LFR", 13, ChannelPlatform::kRadiko},  // 2027
    {"ラジオNIKKEI第1", "RN1", 13, ChannelPlatform::kRadiko},  // 2028
    {"ラジオNIKKEI第2", "RN2", 13, ChannelPlatform::kRadiko},  // 2029
    {"ラジオ日本", "JORF", 13, ChannelPlatform::kRadiko},  // 2030
    {"文化放送", "QRR", 13, ChannelPlatform::kRadiko},  // 2031
    // JP14 神奈川
    {"ＦＭヨコハマ", "YFM", 14, ChannelPlatform::kRadiko},  // 2032
    // JP15 新潟
    {"FM NIIGATA", "FMNIIGATA", 15, ChannelPlatform::kRadiko},  // 2033
    {"ＢＳＮラジオ", "BSN", 15, ChannelPlatform::kRadiko},  // 2034
    // JP16 富山
    {"ＦＭとやま", "FMTOYAMA", 16, ChannelPlatform::kRadiko},  // 2035
    {"ＫＮＢラジオ", "KNB", 16, ChannelPlatform::kRadiko},  // 2036
    // JP17 石川
    {"MROラジオ", "MRO", 17, ChannelPlatform::kRadiko},  // 2037
    {"エフエム石川", "HELLOFIVE", 17, ChannelPlatform::kRadiko},  // 2038
    // JP18 福井
    {"FBCラジオ", "FBC", 18, ChannelPlatform::kRadiko},  // 2039
    {"FM福井", "FMFUKUI", 18, ChannelPlatform::kRadiko},  // 2040
    // JP19 山梨
    {"FM FUJI", "FM-FUJI", 19, ChannelPlatform::kRadiko},  // 2041
    {"ＹＢＳラジオ", "YBS", 19, ChannelPlatform::kRadiko},  // 2042
    // JP20 長野
    {"SBCラジオ", "SBC", 20, ChannelPlatform::kRadiko},  // 2043
    {"ＦＭ長野", "FMN", 20, ChannelPlatform::kRadiko},  // 2044
    // JP21 岐阜
    {"ぎふチャン", "GBS", 21, ChannelPlatform::kRadiko},  // 2045
    {"ＦＭ ＧＩＦＵ", "FMGIFU", 21, ChannelPlatform::kRadiko},  // 2046
    // JP22 静岡
    {"K-MIX", "K-MIX", 22, ChannelPlatform::kRadiko},  // 2047
    {"SBSラジオ", "SBS", 22, ChannelPlatform::kRadiko},  // 2048
    // JP23 愛知
    {"CBCラジオ", "CBC", 23, ChannelPlatform::kRadiko},  // 2049
    {"FM AICHI", "FMAICHI", 23, ChannelPlatform::kRadiko},  // 2050
    {"TOKAI RADIO", "TOKAIRADIO", 23, ChannelPlatform::kRadiko},  // 2051
    {"ZIP-FM", "ZIP-FM", 23, ChannelPlatform::kRadiko},  // 2052
    // JP24 三重
    {"レディオキューブ ＦＭ三重", "FMMIE", 24, ChannelPlatform::kRadiko},  // 2053
    // JP25 滋賀
    {"e-radio FM滋賀", "E-RADIO", 25, ChannelPlatform::kRadiko},  // 2054
    // JP26 京都
    {"KBS京都ラジオ", "KBS", 26, ChannelPlatform::kRadiko},  // 2055
    {"α-STATION FM KYOTO", "ALPHA-STATION", 26, ChannelPlatform::kRadiko},  // 2056
    // JP27 大阪
    {"ABCラジオ", "ABC", 27, ChannelPlatform::kRadiko},  // 2057
    {"FM COCOLO", "CCL", 27, ChannelPlatform::kRadiko},  // 2058
    {"FM802", "802", 27, ChannelPlatform::kRadiko},  // 2059
    {"FM大阪", "FMO", 27, ChannelPlatform::kRadiko},  // 2060
    {"MBSラジオ", "MBS", 27, ChannelPlatform::kRadiko},  // 2061
    {"OBCラジオ大阪", "OBC", 27, ChannelPlatform::kRadiko},  // 2062
    // JP28 兵庫
    {"Kiss FM KOBE", "KISSFMKOBE", 28, ChannelPlatform::kRadiko},  // 2063
    {"ラジオ関西", "CRK", 28, ChannelPlatform::kRadiko},  // 2064
    // JP30 和歌山
    {"wbs和歌山放送", "WBS", 30, ChannelPlatform::kRadiko},  // 2065
    // JP31 鳥取
    {"BSSラジオ", "BSS", 31, ChannelPlatform::kRadiko},  // 2066
    // JP32 島根
    {"エフエム山陰", "FM-SANIN", 32, ChannelPlatform::kRadiko},  // 2067
    // JP33 岡山
    {"ＦＭ岡山", "FM-OKAYAMA", 33, ChannelPlatform::kRadiko},  // 2068
    {"ＲＳＫラジオ", "RSK", 33, ChannelPlatform::kRadiko},  // 2069
    // JP34 広島
    {"RCCラジオ", "RCC", 34, ChannelPlatform::kRadiko},  // 2070
    {"広島FM", "HFM", 34, ChannelPlatform::kRadiko},  // 2071
    // JP35 山口
    {"エフエム山口", "FMY", 35, ChannelPlatform::kRadiko},  // 2072
    {"ＫＲＹ山口放送", "KRY", 35, ChannelPlatform::kRadiko},  // 2073
    // JP36 徳島
    {"FM徳島", "FM807", 36, ChannelPlatform::kRadiko},  // 2074
    {"ＪＲＴ四国放送", "JRT", 36, ChannelPlatform::kRadiko},  // 2075
    // JP37 香川
    {"RNC西日本放送", "RNC", 37, ChannelPlatform::kRadiko},  // 2076
    {"エフエム香川", "FMKAGAWA", 37, ChannelPlatform::kRadiko},  // 2077
    // JP38 愛媛
    {"FM愛媛", "JOEU-FM", 38, ChannelPlatform::kRadiko},  // 2078
    {"RNB南海放送", "RNB", 38, ChannelPlatform::kRadiko},  // 2079
    // JP39 高知
    {"RKC高知放送", "RKC", 39, ChannelPlatform::kRadiko},  // 2080
    {"エフエム高知", "HI-SIX", 39, ChannelPlatform::kRadiko},  // 2081
    // JP40 福岡
    {"CROSS FM", "CROSSFM", 40, ChannelPlatform::kRadiko},  // 2082
    {"FM FUKUOKA", "FMFUKUOKA", 40, ChannelPlatform::kRadiko},  // 2083
    {"KBCラジオ", "KBC", 40, ChannelPlatform::kRadiko},  // 2084
    {"LOVE FM", "LOVEFM", 40, ChannelPlatform::kRadiko},  // 2085
    {"RKBラジオ", "RKB", 40, ChannelPlatform::kRadiko},  // 2086
    // JP41 佐賀
    {"エフエム佐賀", "FMS", 41, ChannelPlatform::kRadiko},  // 2087
    // JP42 長崎
    {"FM長崎", "FMNAGASAKI", 42, ChannelPlatform::kRadiko},  // 2088
    {"NBCラジオ", "NBC", 42, ChannelPlatform::kRadiko},  // 2089
    // JP43 熊本
    {"FMKエフエム熊本", "FMK", 43, ChannelPlatform::kRadiko},  // 2090
    {"RKKラジオ", "RKK", 43, ChannelPlatform::kRadiko},  // 2091
    // JP44 大分
    {"OBSラジオ", "OBS", 44, ChannelPlatform::kRadiko},  // 2092
    {"エフエム大分", "FM_OITA", 44, ChannelPlatform::kRadiko},  // 2093
    // JP45 宮崎
    {"エフエム宮崎", "JOYFM", 45, ChannelPlatform::kRadiko},  // 2094
    {"宮崎放送", "MRT", 45, ChannelPlatform::kRadiko},  // 2095
    // JP46 鹿児島
    {"μＦＭ", "MYUFM", 46, ChannelPlatform::kRadiko},  // 2096
    {"ＭＢＣラジオ", "MBC", 46, ChannelPlatform::kRadiko},  // 2097
    // JP47 沖縄
    {"FM沖縄", "FM_OKINAWA", 47, ChannelPlatform::kRadiko},  // 2098
    {"RBCiラジオ", "RBC", 47, ChannelPlatform::kRadiko},  // 2099
    {"ラジオ沖縄", "ROK", 47, ChannelPlatform::kRadiko},  // 2100
    // ==== 3: JCBA (145局) ====
    // JP1 北海道
    {"FMいるか", "iruka", 1, ChannelPlatform::kJcba},  // 3001
    {"FMくりやま", "fmkuriyama", 1, ChannelPlatform::kJcba},  // 3002
    {"FMとまこまい", "fmtomakomai", 1, ChannelPlatform::kJcba},  // 3003
    {"FMびゅー", "muroran", 1, ChannelPlatform::kJcba},  // 3004
    {"FMりべーる", "asahikawacitynetwork", 1, ChannelPlatform::kJcba},  // 3005
    {"エフエムもえる", "moeru", 1, ChannelPlatform::kJcba},  // 3006
    {"ラジオニセコ", "radioniseko", 1, ChannelPlatform::kJcba},  // 3007
    {"Ａｉｒてっし", "airtesshi", 1, ChannelPlatform::kJcba},  // 3008
    {"ＦＭはな", "fmhana", 1, ChannelPlatform::kJcba},  // 3009
    // JP2 青森
    {"FM AZUR", "fmazur", 2, ChannelPlatform::kJcba},  // 3010
    {"FMごしょがわら", "fmgoshogawara", 2, ChannelPlatform::kJcba},  // 3011
    {"アップルウェーブ", "applewave", 2, ChannelPlatform::kJcba},  // 3012
    // JP3 岩手
    {"FMONE", "fmone", 3, ChannelPlatform::kJcba},  // 3013
    // JP4 宮城
    {"エフエムいわぬま", "fmiwanuma", 4, ChannelPlatform::kJcba},  // 3014
    {"Ｈ＠！ＦＭ", "hatfm", 4, ChannelPlatform::kJcba},  // 3015
    // JP6 山形
    {"えふえむい～じゃんおらんだらじお", "orandaradio", 6, ChannelPlatform::kJcba},  // 3016
    {"エフエムNCV", "yonezawancvfm", 6, ChannelPlatform::kJcba},  // 3017
    {"ハーバーラジオ", "harborradio", 6, ChannelPlatform::kJcba},  // 3018
    {"ラジオ モンスター", "radiomonster", 6, ChannelPlatform::kJcba},  // 3019
    // JP7 福島
    {"FM愛'S", "fmaizu", 7, ChannelPlatform::kJcba},  // 3020
    {"ウルトラＦＭ", "ultrafm", 7, ChannelPlatform::kJcba},  // 3021
    {"エフエムきたかた", "fmkitakata", 7, ChannelPlatform::kJcba},  // 3022
    {"ＦＭポコ", "fmpoco", 7, ChannelPlatform::kJcba},  // 3023
    // JP8 茨城
    {"FMかしま", "fmkashima", 8, ChannelPlatform::kJcba},  // 3024
    {"ＦＭだいご", "fmdaigo", 8, ChannelPlatform::kJcba},  // 3025
    // JP10 群馬
    {"エフエム太郎", "fmtaro", 10, ChannelPlatform::kJcba},  // 3026
    {"ラジオ高崎", "radiotakasaki", 10, ChannelPlatform::kJcba},  // 3027
    {"ラヂオななみ", "radionanami", 10, ChannelPlatform::kJcba},  // 3028
    {"ＦＭ ＯＺＥ", "fmoze", 10, ChannelPlatform::kJcba},  // 3029
    // JP11 埼玉
    {"ラジオ川越", "radiokawagoe", 11, ChannelPlatform::kJcba},  // 3030
    {"発するFM", "miyoshifm", 11, ChannelPlatform::kJcba},  // 3031
    {"ＦＭチャッピー", "fmchappy", 11, ChannelPlatform::kJcba},  // 3032
    // JP12 千葉
    {"FMふくろう", "fmfukuro", 12, ChannelPlatform::kJcba},  // 3033
    {"SKYWAVE FM", "skywavefm", 12, ChannelPlatform::kJcba},  // 3034
    {"かずさFM", "kazusafm", 12, ChannelPlatform::kJcba},  // 3035
    {"ラジオ成田", "radionarita", 12, ChannelPlatform::kJcba},  // 3036
    {"市川うららFM(I&U-LaLaFM)", "ulalafm", 12, ChannelPlatform::kJcba},  // 3037
    // JP13 東京
    {"FMしながわ", "fmshinagawa", 13, ChannelPlatform::kJcba},  // 3038
    {"かつしかFM", "fmkatsushika", 13, ChannelPlatform::kJcba},  // 3039
    {"むさしのＦＭ", "musashinofm", 13, ChannelPlatform::kJcba},  // 3040
    {"コマラジ", "komaraji", 13, ChannelPlatform::kJcba},  // 3041
    {"中央エフエム・RADIO CITY", "radiocity", 13, ChannelPlatform::kJcba},  // 3042
    {"渋谷のラジオ", "shibuyanoradio", 13, ChannelPlatform::kJcba},  // 3043
    {"ＦＭえどがわ", "fmedogawa", 13, ChannelPlatform::kJcba},  // 3044
    // JP14 神奈川
    {"FMおだわら", "fmodawara", 14, ChannelPlatform::kJcba},  // 3045
    {"FMやまと", "fmyamato", 14, ChannelPlatform::kJcba},  // 3046
    {"FMサルース", "fmsalus", 14, ChannelPlatform::kJcba},  // 3047
    {"FM湘南ナパサ", "fmshonan", 14, ChannelPlatform::kJcba},  // 3048
    {"FM湘南マジックウェイブ", "magicwave", 14, ChannelPlatform::kJcba},  // 3049
    {"エフエム戸塚", "fmtotsuka", 14, ChannelPlatform::kJcba},  // 3050
    {"エボラジ", "chigasakifm", 14, ChannelPlatform::kJcba},  // 3051
    {"マリンFM", "marinefm", 14, ChannelPlatform::kJcba},  // 3052
    {"レディオ湘南", "radioshonan", 14, ChannelPlatform::kJcba},  // 3053
    {"鎌倉FM", "kamakurafm", 14, ChannelPlatform::kJcba},  // 3054
    {"関東臨時災害放送局訓練", "rinsaikanto", 14, ChannelPlatform::kJcba},  // 3055
    {"ＦＭブルー湘南", "fmblueshonan", 14, ChannelPlatform::kJcba},  // 3056
    // JP15 新潟
    {"FM KENTO", "fmkento", 15, ChannelPlatform::kJcba},  // 3057
    {"FMうおぬま", "fmuonuma", 15, ChannelPlatform::kJcba},  // 3058
    {"FMじょうえつ", "fmjyoetsu", 15, ChannelPlatform::kJcba},  // 3059
    {"FMピッカラ", "fmpikkara", 15, ChannelPlatform::kJcba},  // 3060
    {"エフエムしばた", "fmshibata", 15, ChannelPlatform::kJcba},  // 3061
    {"エフエムながおか", "fmnagaoka", 15, ChannelPlatform::kJcba},  // 3062
    {"ラジオチャット・FMにいつ", "radiochat", 15, ChannelPlatform::kJcba},  // 3063
    {"ＦＭゆきぐに", "fmyukiguni", 15, ChannelPlatform::kJcba},  // 3064
    // JP16 富山
    {"エフエムとなみ", "fmtonami", 16, ChannelPlatform::kJcba},  // 3065
    {"ラジオたかおか", "radiotakaoka", 16, ChannelPlatform::kJcba},  // 3066
    {"富山シティエフエム株式会社", "toyamacityfm", 16, ChannelPlatform::kJcba},  // 3067
    // JP17 石川
    {"ラジオかなざわ", "radiokanazawa", 17, ChannelPlatform::kJcba},  // 3068
    {"ラジオこまつ", "radiokomatsu", 17, ChannelPlatform::kJcba},  // 3069
    {"ラジオななお", "radionanao", 17, ChannelPlatform::kJcba},  // 3070
    // JP19 山梨
    {"FMふじやま", "fmfujiyama", 19, ChannelPlatform::kJcba},  // 3071
    {"FM八ヶ岳", "fmyatsugatake", 19, ChannelPlatform::kJcba},  // 3072
    {"エフエム　ふじごこ", "fmfujigoko", 19, ChannelPlatform::kJcba},  // 3073
    {"エフエム甲府", "fmkofu", 19, ChannelPlatform::kJcba},  // 3074
    // JP20 長野
    {"FM軽井沢", "fmkaruizawa", 20, ChannelPlatform::kJcba},  // 3075
    {"LCV FM", "lovefm", 20, ChannelPlatform::kJcba},  // 3076
    {"伊那谷FM", "inadanifm", 20, ChannelPlatform::kJcba},  // 3077
    {"高ボッチ高原FM", "shiojirifm", 20, ChannelPlatform::kJcba},  // 3078
    // JP21 岐阜
    {"ＦＭわっち", "fmwatch", 21, ChannelPlatform::kJcba},  // 3079
    {"ＦＭＰｉＰｉ", "fmpipi", 21, ChannelPlatform::kJcba},  // 3080
    {"Ｈｉｔｓ ＦＭ", "hitsfm", 21, ChannelPlatform::kJcba},  // 3081
    // JP22 静岡
    {"COAST-FM76.7MH z", "coastfm", 22, ChannelPlatform::kJcba},  // 3082
    {"Ciao!", "ciao", 22, ChannelPlatform::kJcba},  // 3083
    {"FM Haro!", "haro", 22, ChannelPlatform::kJcba},  // 3084
    {"FM ISみらいずステーション", "fmis", 22, ChannelPlatform::kJcba},  // 3085
    {"FMいずのくに", "fmizunokuni", 22, ChannelPlatform::kJcba},  // 3086
    {"RADIO LUSH", "fmyaizu", 22, ChannelPlatform::kJcba},  // 3087
    {"Radio-f", "radiof", 22, ChannelPlatform::kJcba},  // 3088
    {"S-Wave", "swave", 22, ChannelPlatform::kJcba},  // 3089
    {"g-sky76.5", "fmshimada", 22, ChannelPlatform::kJcba},  // 3090
    {"エフエムなぎさステーション", "nagisastation", 22, ChannelPlatform::kJcba},  // 3091
    {"ボイスキュー", "voicecue", 22, ChannelPlatform::kJcba},  // 3092
    {"富士山ＧＯＧＯＦＭ", "fujiyamagogofm", 22, ChannelPlatform::kJcba},  // 3093
    // JP23 愛知
    {"HeartFM", "heartfm", 23, ChannelPlatform::kJcba},  // 3094
    {"RADIO SANQ", "radiosanq", 23, ChannelPlatform::kJcba},  // 3095
    {"United North", "unitednorth", 23, ChannelPlatform::kJcba},  // 3096
    {"i-wave", "fmichinomiya", 23, ChannelPlatform::kJcba},  // 3097
    {"エフエム　ななみ", "fmnanami", 23, ChannelPlatform::kJcba},  // 3098
    // JP24 三重
    {"Suzuka Voice FM 78.3MHz", "suzuka", 24, ChannelPlatform::kJcba},  // 3099
    {"いなべエフエム", "inabefm", 24, ChannelPlatform::kJcba},  // 3100
    // JP25 滋賀
    {"えふえむ草津", "fmkusatsu", 25, ChannelPlatform::kJcba},  // 3101
    // JP26 京都
    {"FM845", "kyotoribingufm", 26, ChannelPlatform::kJcba},  // 3102
    {"FMうじ", "fmuji", 26, ChannelPlatform::kJcba},  // 3103
    {"FMまいづる", "fmmaizuru", 26, ChannelPlatform::kJcba},  // 3104
    {"ＦＭいかる", "fmikaru", 26, ChannelPlatform::kJcba},  // 3105
    // JP27 大阪
    {"FM千里", "fmsenri", 27, ChannelPlatform::kJcba},  // 3106
    {"ウメダFM Be Happy!789", "umedafm", 27, ChannelPlatform::kJcba},  // 3107
    {"タッキー816みのおエフエム", "minofm", 27, ChannelPlatform::kJcba},  // 3108
    // JP28 兵庫
    {"805たんば", "tanba", 28, ChannelPlatform::kJcba},  // 3109
    {"BAN-BANラジオ", "banbanradio", 28, ChannelPlatform::kJcba},  // 3110
    {"FM GENKI", "fmgenki", 28, ChannelPlatform::kJcba},  // 3111
    {"さくらFM", "sakurafm", 28, ChannelPlatform::kJcba},  // 3112
    {"エフエムいたみ", "fmitami", 28, ChannelPlatform::kJcba},  // 3113
    {"エフエムみっきぃ", "fmmiki", 28, ChannelPlatform::kJcba},  // 3114
    {"ハミングFM宝塚", "fmtakarazuka", 28, ChannelPlatform::kJcba},  // 3115
    // JP29 奈良
    {"FMまほろば", "fmmahoroba", 29, ChannelPlatform::kJcba},  // 3116
    {"なら どっと ＦＭ", "narafm", 29, ChannelPlatform::kJcba},  // 3117
    {"ＦＭ五條", "fmgojo", 29, ChannelPlatform::kJcba},  // 3118
    // JP30 和歌山
    {"FM TANABE", "fmtanabe", 30, ChannelPlatform::kJcba},  // 3119
    {"FMはしもと", "fmhashimoto", 30, ChannelPlatform::kJcba},  // 3120
    {"FMビーチステーション", "beachstation", 30, ChannelPlatform::kJcba},  // 3121
    {"FM新宮", "fmshingu", 30, ChannelPlatform::kJcba},  // 3122
    {"バナナエフエム", "bananafm", 30, ChannelPlatform::kJcba},  // 3123
    // JP31 鳥取
    {"RADIO BIRD", "radiobird", 31, ChannelPlatform::kJcba},  // 3124
    // JP33 岡山
    {"FMくらしき", "fmkurashiki", 33, ChannelPlatform::kJcba},  // 3125
    {"レディオ モモ", "radiomomo", 33, ChannelPlatform::kJcba},  // 3126
    // JP34 広島
    {"FMおのみち", "fmonomichi", 34, ChannelPlatform::kJcba},  // 3127
    {"FMはつかいち", "fmhatsukaichi", 34, ChannelPlatform::kJcba},  // 3128
    {"FMふくやま", "bingo", 34, ChannelPlatform::kJcba},  // 3129
    {"FM東広島", "fmhigashihiroshima", 34, ChannelPlatform::kJcba},  // 3130
    {"FOR LIFE RADIO", "fmmihara", 34, ChannelPlatform::kJcba},  // 3131
    {"ＦＭミコリン", "fmchupea", 34, ChannelPlatform::kJcba},  // 3132
    // JP35 山口
    {"しゅうなんＦＭ", "shunanfm", 35, ChannelPlatform::kJcba},  // 3133
    {"ＣＯＭＥ ＯＮ ! ＦＭ", "comeonfm", 35, ChannelPlatform::kJcba},  // 3134
    // JP37 香川
    {"エフエム・サン", "fmsun", 37, ChannelPlatform::kJcba},  // 3135
    // JP38 愛媛
    {"FMがいや", "fmgaiya", 38, ChannelPlatform::kJcba},  // 3136
    {"FMラヂオバリバリ", "fmradiobaribari", 38, ChannelPlatform::kJcba},  // 3137
    {"Hello! NEW 新居浜 FM", "niihamafm", 38, ChannelPlatform::kJcba},  // 3138
    // JP40 福岡
    {"Dreams FM", "dreamsfm", 40, ChannelPlatform::kJcba},  // 3139
    {"FM八女", "fmyame", 40, ChannelPlatform::kJcba},  // 3140
    // JP41 佐賀
    {"ＦＭからつ", "fmkaratsu", 41, ChannelPlatform::kJcba},  // 3141
    // JP43 熊本
    {"FM791", "kumamotocityfm", 43, ChannelPlatform::kJcba},  // 3142
    {"Kappa FM", "fmyatsushiro", 43, ChannelPlatform::kJcba},  // 3143
    // JP44 大分
    {"NOASFM", "noasfm", 44, ChannelPlatform::kJcba},  // 3144
    {"ゆふいんラヂオ局", "yufuin", 44, ChannelPlatform::kJcba},  // 3145
    // ==== 4: ListenRadio (78局) ====
    // JP1 北海道
    {"FM ABASHIRI", "30058", 1, ChannelPlatform::kListenRadio},  // 4001
    {"FM G'Sky", "30003", 1, ChannelPlatform::kListenRadio},  // 4002
    {"FM JAGA", "30016", 1, ChannelPlatform::kListenRadio},  // 4003
    {"FM WING", "30038", 1, ChannelPlatform::kListenRadio},  // 4004
    {"FMおたる", "30025", 1, ChannelPlatform::kListenRadio},  // 4005
    {"FMくしろ", "30029", 1, ChannelPlatform::kListenRadio},  // 4006
    {"FMねむろ", "30045", 1, ChannelPlatform::kListenRadio},  // 4007
    {"FMはまなす", "30004", 1, ChannelPlatform::kListenRadio},  // 4008
    {"FMわっぴー", "30011", 1, ChannelPlatform::kListenRadio},  // 4009
    {"FMアップル", "30090", 1, ChannelPlatform::kListenRadio},  // 4010
    {"FMメイプルきたひろボールパークラジオ", "30015", 1, ChannelPlatform::kListenRadio},  // 4011
    {"RadioYAMASHO FMドラマシティ", "30044", 1, ChannelPlatform::kListenRadio},  // 4012
    {"wi-radio", "30087", 1, ChannelPlatform::kListenRadio},  // 4013
    {"さっぽろ村ラジオ", "30032", 1, ChannelPlatform::kListenRadio},  // 4014
    {"ラジオカロスサッポロ", "30034", 1, ChannelPlatform::kListenRadio},  // 4015
    {"三角山放送局（札幌市西区）", "30005", 1, ChannelPlatform::kListenRadio},  // 4016
    // JP2 青森
    {"BeFM", "30079", 2, ChannelPlatform::kListenRadio},  // 4017
    // JP3 岩手
    {"みやこハーバーラジオ", "30097", 3, ChannelPlatform::kListenRadio},  // 4018
    {"カシオペアFM", "30050", 3, ChannelPlatform::kListenRadio},  // 4019
    {"ラヂオもりおか", "30017", 3, ChannelPlatform::kListenRadio},  // 4020
    // JP4 宮城
    {"BAY WAVE", "30056", 4, ChannelPlatform::kListenRadio},  // 4021
    {"RADIO3", "30007", 4, ChannelPlatform::kListenRadio},  // 4022
    {"fmいずみ", "30018", 4, ChannelPlatform::kListenRadio},  // 4023
    {"なとらじ801", "30092", 4, ChannelPlatform::kListenRadio},  // 4024
    {"ラジオ石巻", "30037", 4, ChannelPlatform::kListenRadio},  // 4025
    {"ラヂオ気仙沼", "30094", 4, ChannelPlatform::kListenRadio},  // 4026
    // JP5 秋田
    {"FMゆーとぴあ", "30030", 5, ChannelPlatform::kListenRadio},  // 4027
    {"エフエム椿台", "30014", 5, ChannelPlatform::kListenRadio},  // 4028
    {"横手かまくらエフエム", "30076", 5, ChannelPlatform::kListenRadio},  // 4029
    {"鹿角きりたんぽFM", "30089", 5, ChannelPlatform::kListenRadio},  // 4030
    // JP7 福島
    {"FM Mot.com", "30019", 7, ChannelPlatform::kListenRadio},  // 4031
    {"FMいわき", "30009", 7, ChannelPlatform::kListenRadio},  // 4032
    {"KOCOラジ", "30020", 7, ChannelPlatform::kListenRadio},  // 4033
    // JP8 茨城
    {"FMうしくうれしく放送", "30021", 8, ChannelPlatform::kListenRadio},  // 4034
    {"FMぱるるん", "30022", 8, ChannelPlatform::kListenRadio},  // 4035
    {"FMひたち", "30023", 8, ChannelPlatform::kListenRadio},  // 4036
    // JP10 群馬
    {"まえばしラジオ", "30043", 10, ChannelPlatform::kListenRadio},  // 4037
    // JP11 埼玉
    {"775ライブリーFM", "30026", 11, ChannelPlatform::kListenRadio},  // 4038
    {"FM Kawaguchi", "30035", 11, ChannelPlatform::kListenRadio},  // 4039
    {"FMふっかちゃん", "30047", 11, ChannelPlatform::kListenRadio},  // 4040
    {"FMわたらせ", "30048", 11, ChannelPlatform::kListenRadio},  // 4041
    {"REDS WAVE", "30008", 11, ChannelPlatform::kListenRadio},  // 4042
    {"ハローハッピー・こしがやエフエム", "30096", 11, ChannelPlatform::kListenRadio},  // 4043
    {"フラワーラジオ", "30002", 11, ChannelPlatform::kListenRadio},  // 4044
    // JP13 東京
    {"Cwave", "30051", 13, ChannelPlatform::kListenRadio},  // 4045
    {"FMたちかわ", "30033", 13, ChannelPlatform::kListenRadio},  // 4046
    {"Tokyo Star Radio(八王子FM)", "30081", 13, ChannelPlatform::kListenRadio},  // 4047
    {"エフエム世田谷", "30027", 13, ChannelPlatform::kListenRadio},  // 4048
    {"レインボータウンFM", "30036", 13, ChannelPlatform::kListenRadio},  // 4049
    {"調布FM", "30039", 13, ChannelPlatform::kListenRadio},  // 4050
    // JP14 神奈川
    {"ＦＭカオン", "30057", 14, ChannelPlatform::kListenRadio},  // 4051
    // JP16 富山
    {"ラジオ・ミュー", "30006", 16, ChannelPlatform::kListenRadio},  // 4052
    // JP17 石川
    {"FM N1", "30001", 17, ChannelPlatform::kListenRadio},  // 4053
    // JP18 福井
    {"敦賀FM", "30012", 18, ChannelPlatform::kListenRadio},  // 4054
    // JP23 愛知
    {"エフエムEGAO", "30040", 23, ChannelPlatform::kListenRadio},  // 4055
    // JP25 滋賀
    {"エフエム花", "30067", 25, ChannelPlatform::kListenRadio},  // 4056
    {"ラジオスイート", "30061", 25, ChannelPlatform::kListenRadio},  // 4057
    // JP26 京都
    {"FM87.0 RADIO MIX KYOTO", "30071", 26, ChannelPlatform::kListenRadio},  // 4058
    {"FMおとくに", "30063", 26, ChannelPlatform::kListenRadio},  // 4059
    {"FMたんご", "30073", 26, ChannelPlatform::kListenRadio},  // 4060
    {"京都三条ラジオカフェ", "30082", 26, ChannelPlatform::kListenRadio},  // 4061
    // JP28 兵庫
    {"BAN-BANラジオ", "30078", 28, ChannelPlatform::kListenRadio},  // 4062
    {"FM ジャングル", "30013", 28, ChannelPlatform::kListenRadio},  // 4063
    // JP31 鳥取
    {"DARAZ　FM", "30053", 31, ChannelPlatform::kListenRadio},  // 4064
    // JP36 徳島
    {"FMびざん", "30010", 36, ChannelPlatform::kListenRadio},  // 4065
    // JP37 香川
    {"FM815（高松）", "30024", 37, ChannelPlatform::kListenRadio},  // 4066
    // JP40 福岡
    {"AIR STATION HIBIKI", "30052", 40, ChannelPlatform::kListenRadio},  // 4067
    {"チョクラジ", "30085", 40, ChannelPlatform::kListenRadio},  // 4068
    // JP45 宮崎
    {"FMのべおか", "30088", 45, ChannelPlatform::kListenRadio},  // 4069
    // JP46 鹿児島
    {"あまみエフエム", "30054", 46, ChannelPlatform::kListenRadio},  // 4070
    {"エフエムたつごう", "30072", 46, ChannelPlatform::kListenRadio},  // 4071
    // JP47 沖縄
    {"FMぎのわん", "30093", 47, ChannelPlatform::kListenRadio},  // 4072
    {"FMとよみ", "30083", 47, ChannelPlatform::kListenRadio},  // 4073
    {"FMやんばる", "30095", 47, ChannelPlatform::kListenRadio},  // 4074
    {"FMよなばる", "30091", 47, ChannelPlatform::kListenRadio},  // 4075
    {"fm那覇", "30068", 47, ChannelPlatform::kListenRadio},  // 4076
    {"ぎのわんシティFM", "30098", 47, ChannelPlatform::kListenRadio},  // 4077
    {"オキラジ", "30066", 47, ChannelPlatform::kListenRadio},  // 4078
    // ==== 5: FMぷらぷら (73局) ====
    // JP1 北海道
    {"ラジオカロスサッポロ", "radiokaros", 1, ChannelPlatform::kFmPlapla},  // 5001
    // JP3 岩手
    {"FMあすも", "fmasmo", 3, ChannelPlatform::kFmPlapla},  // 5002
    {"きたかみE&Beエフエム", "iianbefm", 3, ChannelPlatform::kFmPlapla},  // 5003
    // JP4 宮城
    {"ＯＣＲFM835", "bikkifm", 4, ChannelPlatform::kFmPlapla},  // 5004
    // JP5 秋田
    {"FMはなび", "fmhanabi", 5, ChannelPlatform::kFmPlapla},  // 5005
    // JP9 栃木
    {"FM DAMONO", "fmdamono", 9, ChannelPlatform::kFmPlapla},  // 5006
    {"FMくらら857", "fmkulala857", 9, ChannelPlatform::kFmPlapla},  // 5007
    {"FMもおか", "fmmoka", 9, ChannelPlatform::kFmPlapla},  // 5008
    {"FMゆうがお", "fmyugao", 9, ChannelPlatform::kFmPlapla},  // 5009
    {"おーラジ", "oradi", 9, ChannelPlatform::kFmPlapla},  // 5010
    {"ミヤラジ", "miyaradi", 9, ChannelPlatform::kFmPlapla},  // 5011
    // JP10 群馬
    {"FM桐生", "fmkiryu", 10, ChannelPlatform::kFmPlapla},  // 5012
    // JP11 埼玉
    {"FMクマガヤ", "fmkumagaya", 11, ChannelPlatform::kFmPlapla},  // 5013
    {"あいラジ", "airadi", 11, ChannelPlatform::kFmPlapla},  // 5014
    {"ちちぶFM", "chichibufm", 11, ChannelPlatform::kFmPlapla},  // 5015
    // JP13 東京
    {"Hello FM", "hellofm", 13, ChannelPlatform::kFmPlapla},  // 5016
    {"TOKYO854 くるめラ", "fmhigashikurume", 13, ChannelPlatform::kFmPlapla},  // 5017
    {"エフエム西東京", "fmnishitokyo", 13, ChannelPlatform::kFmPlapla},  // 5018
    {"ラジオTAMAリバー", "radiofuchues", 13, ChannelPlatform::kFmPlapla},  // 5019
    // JP14 神奈川
    {"FM大師", "fmdaishi", 14, ChannelPlatform::kFmPlapla},  // 5020
    {"かわさきエフエム", "kawasakifm", 14, ChannelPlatform::kFmPlapla},  // 5021
    {"金沢シーサイドFM", "kanazawaseasidefm", 14, ChannelPlatform::kFmPlapla},  // 5022
    // JP15 新潟
    {"FMとおかまち", "fmtokamachi", 15, ChannelPlatform::kFmPlapla},  // 5023
    {"FMながおか", "fmnagaoka", 15, ChannelPlatform::kFmPlapla},  // 5024
    {"FMみょうこう", "fmmyoko", 15, ChannelPlatform::kFmPlapla},  // 5025
    {"ラヂオは〜と", "radioheart", 15, ChannelPlatform::kFmPlapla},  // 5026
    // JP17 石川
    {"まちのラジオ(災害FM)", "machinoradio", 17, ChannelPlatform::kFmPlapla},  // 5027
    // JP20 長野
    {"FMぜんこうじ", "fmzenkouji", 20, ChannelPlatform::kFmPlapla},  // 5028
    {"FMまつもと", "fmmatsumoto", 20, ChannelPlatform::kFmPlapla},  // 5029
    {"fmさくだいら", "fmsakudaira", 20, ChannelPlatform::kFmPlapla},  // 5030
    {"あづみ野エフエム", "azuminofm", 20, ChannelPlatform::kFmPlapla},  // 5031
    {"いいだFM", "iidafm", 20, ChannelPlatform::kFmPlapla},  // 5032
    {"はれラジ", "fmtomi", 20, ChannelPlatform::kFmPlapla},  // 5033
    // JP21 岐阜
    {"FMらら76.8", "fmrara768", 21, ChannelPlatform::kFmPlapla},  // 5034
    // JP23 愛知
    {"KATCH&Pitch 地域情報", "pitchfm", 23, ChannelPlatform::kFmPlapla},  // 5035
    {"TEES-843FM", "tees-843fm", 23, ChannelPlatform::kFmPlapla},  // 5036
    {"メディアスFM", "mediasfm", 23, ChannelPlatform::kFmPlapla},  // 5037
    {"ラジオ・ラブィート", "radioloveat", 23, ChannelPlatform::kFmPlapla},  // 5038
    {"名古屋市防災(MID-FM)", "nagoyabousai", 23, ChannelPlatform::kFmPlapla},  // 5039
    // JP24 三重
    {"CTY-FM", "fmyokkaichi", 24, ChannelPlatform::kFmPlapla},  // 5040
    // JP25 滋賀
    {"FMおおつ", "fmotsu", 25, ChannelPlatform::kFmPlapla},  // 5041
    {"びわこの東のFM", "fmhikone", 25, ChannelPlatform::kFmPlapla},  // 5042
    // JP27 大阪
    {"FMいずみおおつ", "fmizumiotsu", 27, ChannelPlatform::kFmPlapla},  // 5043
    {"YES-fm", "yesfm", 27, ChannelPlatform::kFmPlapla},  // 5044
    {"やおFM", "yaofm", 27, ChannelPlatform::kFmPlapla},  // 5045
    {"奥河内ラジオ", "okukawachi", 27, ChannelPlatform::kFmPlapla},  // 5046
    // JP28 兵庫
    {"FM BLOOM(あま咲き放送局)", "amasakifm", 28, ChannelPlatform::kFmPlapla},  // 5047
    // JP29 奈良
    {"FMハイホー", "fmhaiho", 29, ChannelPlatform::kFmPlapla},  // 5048
    {"FMヤマト", "fmyamato", 29, ChannelPlatform::kFmPlapla},  // 5049
    // JP33 岡山
    {"ゆめウェーブ", "fmyumewave", 33, ChannelPlatform::kFmPlapla},  // 5050
    // JP35 山口
    {"FMわっしょい", "fmwassyoi", 35, ChannelPlatform::kFmPlapla},  // 5051
    {"FMスマイルウェ～ブ", "fmsunsunkirara", 35, ChannelPlatform::kFmPlapla},  // 5052
    // JP39 高知
    {"FMはたらんど", "fmhataland", 39, ChannelPlatform::kFmPlapla},  // 5053
    // JP40 福岡
    {"COMIxTEN", "comiten", 40, ChannelPlatform::kFmPlapla},  // 5054
    {"FM KITAQ", "fmkitaq", 40, ChannelPlatform::kFmPlapla},  // 5055
    {"FMたんと", "fmtanto", 40, ChannelPlatform::kFmPlapla},  // 5056
    // JP41 佐賀
    {"えびすFM", "ebisufm", 41, ChannelPlatform::kFmPlapla},  // 5057
    // JP42 長崎
    {"FMさせぼ", "happyfm", 42, ChannelPlatform::kFmPlapla},  // 5058
    {"FMしまばら", "fmshimabara", 42, ChannelPlatform::kFmPlapla},  // 5059
    {"FMひまわり", "fmhimawari", 42, ChannelPlatform::kFmPlapla},  // 5060
    {"Silk FM", "fmomura", 42, ChannelPlatform::kFmPlapla},  // 5061
    {"エフエム対馬", "fmtsushima", 42, ChannelPlatform::kFmPlapla},  // 5062
    {"エフエム諫早", "fmisahaya", 42, ChannelPlatform::kFmPlapla},  // 5063
    // JP43 熊本
    {"みつばちラジオ", "mitsubachiradio", 43, ChannelPlatform::kFmPlapla},  // 5064
    // JP44 大分
    {"エフエムきつき802", "fmkitsuki", 44, ChannelPlatform::kFmPlapla},  // 5065
    // JP45 宮崎
    {"サンシャインFM", "sunshinefm", 45, ChannelPlatform::kFmPlapla},  // 5066
    // JP46 鹿児島
    {"FMかのや", "fmkanoya", 46, ChannelPlatform::kFmPlapla},  // 5067
    {"FMきりしま", "fmkirishima", 46, ChannelPlatform::kFmPlapla},  // 5068
    {"FMさつませんだい", "fmsatsumasendai", 46, ChannelPlatform::kFmPlapla},  // 5069
    {"フレンズFM", "friendsfm", 46, ChannelPlatform::kFmPlapla},  // 5070
    // JP47 沖縄
    {"FMいしがきサンサンラジオ", "fmishigaki", 47, ChannelPlatform::kFmPlapla},  // 5071
    {"FMうるま", "fmuruma", 47, ChannelPlatform::kFmPlapla},  // 5072
    {"FMみやこ", "fmmiyako", 47, ChannelPlatform::kFmPlapla},  // 5073
};

constexpr size_t kStationCount = sizeof(kStations) / sizeof(kStations[0]);

// プラットフォームごとの kStations 上の先頭添字と局数。添字は ChannelPlatform の列挙値
// (= プラットフォーム番号 - 1)。局が無いプラットフォームは count=0。
constexpr PlatformRange kPlatformRanges[] = {
    {0, 16},  // kNhk NHK: 1001-1016
    {16, 100},  // kRadiko radiko: 2001-2100
    {116, 145},  // kJcba JCBA: 3001-3145
    {261, 78},  // kListenRadio ListenRadio: 4001-4078
    {339, 73},  // kFmPlapla FMぷらぷら: 5001-5073
};

static_assert(sizeof(kPlatformRanges) / sizeof(kPlatformRanges[0]) == kChannelPlatformCount,
              "kPlatformRanges must have one entry per ChannelPlatform");

// 各局を無料(非プレミアム)で再生できるエリアのビットマスク。添字は kStations と共通。
// bit (JISコード - 1) が立っていれば、そのエリアの無料リスナーが再生できる
// (radiko の v3/station/list/JPxx.xml への出現から生成)。NHK/JCBA/ListenRadio 行
// (エリア制限なし)、および無料開放が一切無い局(プレミアム専用)は 0。プレミアム認証時は
// この表を参照しない。
constexpr uint64_t kStationFreeAreaMask[] = {
    // ==== 1: NHK ====
    0x0000000000000000ULL,  // 1001 NHKAM（札幌）
    0x0000000000000000ULL,  // 1002 NHKFM（札幌）
    0x0000000000000000ULL,  // 1003 NHKAM（仙台）
    0x0000000000000000ULL,  // 1004 NHKFM（仙台）
    0x0000000000000000ULL,  // 1005 NHKAM（東京）
    0x0000000000000000ULL,  // 1006 NHKFM（東京）
    0x0000000000000000ULL,  // 1007 NHKAM（名古屋）
    0x0000000000000000ULL,  // 1008 NHKFM（名古屋）
    0x0000000000000000ULL,  // 1009 NHKAM（大阪）
    0x0000000000000000ULL,  // 1010 NHKFM（大阪）
    0x0000000000000000ULL,  // 1011 NHKAM（広島）
    0x0000000000000000ULL,  // 1012 NHKFM（広島）
    0x0000000000000000ULL,  // 1013 NHKAM（松山）
    0x0000000000000000ULL,  // 1014 NHKFM（松山）
    0x0000000000000000ULL,  // 1015 NHKAM（福岡）
    0x0000000000000000ULL,  // 1016 NHKFM（福岡）
    // ==== 2: radiko ====
    0x0000000000000001ULL,  // 2001 AIR-G'（FM北海道）
    0x0000000000000001ULL,  // 2002 FM NORTH WAVE
    0x0000000000000001ULL,  // 2003 ＨＢＣラジオ
    0x0000000000000001ULL,  // 2004 ＳＴＶラジオ
    0x0000000000000002ULL,  // 2005 エフエム青森
    0x0000000000000002ULL,  // 2006 ＲＡＢ青森放送
    0x0000000000000004ULL,  // 2007 IBCラジオ
    0x0000000000000004ULL,  // 2008 エフエム岩手
    0x0000000000000008ULL,  // 2009 Date fm エフエム仙台
    0x0000000000000008ULL,  // 2010 tbcラジオ
    0x0000000000000010ULL,  // 2011 ABSラジオ
    0x0000000000000010ULL,  // 2012 エフエム秋田
    0x0000000000000020ULL,  // 2013 Rhythm Station　エフエム山形
    0x0000000000000020ULL,  // 2014 YBC山形放送
    0x0000000000000040ULL,  // 2015 RFCラジオ福島
    0x0000000000000040ULL,  // 2016 ふくしまFM
    0x0000000000003F80ULL,  // 2017 LuckyFM 茨城放送
    0x0000000000000100ULL,  // 2018 CRT栃木放送
    0x0000000000000100ULL,  // 2019 RADIO BERRY
    0x0000000000000200ULL,  // 2020 FM GUNMA
    0x0000000000003F80ULL,  // 2021 NACK5
    0x0000000000003F80ULL,  // 2022 BAYFM78
    0x0000000000003F80ULL,  // 2023 J-WAVE
    0x0000000000003F80ULL,  // 2024 TBSラジオ
    0x0000000000003F80ULL,  // 2025 TOKYO FM
    0x0000000000003F80ULL,  // 2026 interfm
    0x0000000000003F80ULL,  // 2027 ニッポン放送
    0x00007FFFFFFFFFFFULL,  // 2028 ラジオNIKKEI第1
    0x00007FFFFFFFFFFFULL,  // 2029 ラジオNIKKEI第2
    0x0000000000003F80ULL,  // 2030 ラジオ日本
    0x0000000000003F80ULL,  // 2031 文化放送
    0x0000000000003F80ULL,  // 2032 ＦＭヨコハマ
    0x0000000000004000ULL,  // 2033 FM NIIGATA
    0x0000000000004000ULL,  // 2034 ＢＳＮラジオ
    0x0000000000008000ULL,  // 2035 ＦＭとやま
    0x0000000000008000ULL,  // 2036 ＫＮＢラジオ
    0x0000000000010000ULL,  // 2037 MROラジオ
    0x0000000000010000ULL,  // 2038 エフエム石川
    0x0000000000020000ULL,  // 2039 FBCラジオ
    0x0000000000020000ULL,  // 2040 FM福井
    0x0000000000040000ULL,  // 2041 FM FUJI
    0x0000000000040000ULL,  // 2042 ＹＢＳラジオ
    0x0000000000080000ULL,  // 2043 SBCラジオ
    0x0000000000080000ULL,  // 2044 ＦＭ長野
    0x0000000000D00000ULL,  // 2045 ぎふチャン
    0x0000000000100000ULL,  // 2046 ＦＭ ＧＩＦＵ
    0x0000000000200000ULL,  // 2047 K-MIX
    0x0000000000200000ULL,  // 2048 SBSラジオ
    0x0000000000D00000ULL,  // 2049 CBCラジオ
    0x0000000000D00000ULL,  // 2050 FM AICHI
    0x0000000000D00000ULL,  // 2051 TOKAI RADIO
    0x0000000000D00000ULL,  // 2052 ZIP-FM
    0x0000000000800000ULL,  // 2053 レディオキューブ ＦＭ三重
    0x0000000001000000ULL,  // 2054 e-radio FM滋賀
    0x000000003F000000ULL,  // 2055 KBS京都ラジオ
    0x000000003F000000ULL,  // 2056 α-STATION FM KYOTO
    0x000000003F000000ULL,  // 2057 ABCラジオ
    0x000000003F000000ULL,  // 2058 FM COCOLO
    0x000000003F000000ULL,  // 2059 FM802
    0x000000003F000000ULL,  // 2060 FM大阪
    0x000000003F000000ULL,  // 2061 MBSラジオ
    0x000000003F000000ULL,  // 2062 OBCラジオ大阪
    0x000000003F000000ULL,  // 2063 Kiss FM KOBE
    0x000000003F000000ULL,  // 2064 ラジオ関西
    0x0000000024000000ULL,  // 2065 wbs和歌山放送
    0x00000000C0000000ULL,  // 2066 BSSラジオ
    0x00000000C0000000ULL,  // 2067 エフエム山陰
    0x0000000100000000ULL,  // 2068 ＦＭ岡山
    0x0000000100000000ULL,  // 2069 ＲＳＫラジオ
    0x0000000200000000ULL,  // 2070 RCCラジオ
    0x0000000200000000ULL,  // 2071 広島FM
    0x0000000400000000ULL,  // 2072 エフエム山口
    0x0000000400000000ULL,  // 2073 ＫＲＹ山口放送
    0x0000000800000000ULL,  // 2074 FM徳島
    0x0000000800000000ULL,  // 2075 ＪＲＴ四国放送
    0x0000001000000000ULL,  // 2076 RNC西日本放送
    0x0000001000000000ULL,  // 2077 エフエム香川
    0x0000002000000000ULL,  // 2078 FM愛媛
    0x0000002000000000ULL,  // 2079 RNB南海放送
    0x0000004000000000ULL,  // 2080 RKC高知放送
    0x0000004000000000ULL,  // 2081 エフエム高知
    0x0000008000000000ULL,  // 2082 CROSS FM
    0x0000008000000000ULL,  // 2083 FM FUKUOKA
    0x0000018000000000ULL,  // 2084 KBCラジオ
    0x0000008000000000ULL,  // 2085 LOVE FM
    0x0000018000000000ULL,  // 2086 RKBラジオ
    0x0000010000000000ULL,  // 2087 エフエム佐賀
    0x0000020000000000ULL,  // 2088 FM長崎
    0x0000030000000000ULL,  // 2089 NBCラジオ
    0x0000040000000000ULL,  // 2090 FMKエフエム熊本
    0x0000040000000000ULL,  // 2091 RKKラジオ
    0x0000080000000000ULL,  // 2092 OBSラジオ
    0x0000080000000000ULL,  // 2093 エフエム大分
    0x0000100000000000ULL,  // 2094 エフエム宮崎
    0x0000100000000000ULL,  // 2095 宮崎放送
    0x0000200000000000ULL,  // 2096 μＦＭ
    0x0000200000000000ULL,  // 2097 ＭＢＣラジオ
    0x0000400000000000ULL,  // 2098 FM沖縄
    0x0000400000000000ULL,  // 2099 RBCiラジオ
    0x0000400000000000ULL,  // 2100 ラジオ沖縄
    // ==== 3: JCBA ====
    0x0000000000000000ULL,  // 3001 FMいるか
    0x0000000000000000ULL,  // 3002 FMくりやま
    0x0000000000000000ULL,  // 3003 FMとまこまい
    0x0000000000000000ULL,  // 3004 FMびゅー
    0x0000000000000000ULL,  // 3005 FMりべーる
    0x0000000000000000ULL,  // 3006 エフエムもえる
    0x0000000000000000ULL,  // 3007 ラジオニセコ
    0x0000000000000000ULL,  // 3008 Ａｉｒてっし
    0x0000000000000000ULL,  // 3009 ＦＭはな
    0x0000000000000000ULL,  // 3010 FM AZUR
    0x0000000000000000ULL,  // 3011 FMごしょがわら
    0x0000000000000000ULL,  // 3012 アップルウェーブ
    0x0000000000000000ULL,  // 3013 FMONE
    0x0000000000000000ULL,  // 3014 エフエムいわぬま
    0x0000000000000000ULL,  // 3015 Ｈ＠！ＦＭ
    0x0000000000000000ULL,  // 3016 えふえむい～じゃんおらんだらじお
    0x0000000000000000ULL,  // 3017 エフエムNCV
    0x0000000000000000ULL,  // 3018 ハーバーラジオ
    0x0000000000000000ULL,  // 3019 ラジオ モンスター
    0x0000000000000000ULL,  // 3020 FM愛'S
    0x0000000000000000ULL,  // 3021 ウルトラＦＭ
    0x0000000000000000ULL,  // 3022 エフエムきたかた
    0x0000000000000000ULL,  // 3023 ＦＭポコ
    0x0000000000000000ULL,  // 3024 FMかしま
    0x0000000000000000ULL,  // 3025 ＦＭだいご
    0x0000000000000000ULL,  // 3026 エフエム太郎
    0x0000000000000000ULL,  // 3027 ラジオ高崎
    0x0000000000000000ULL,  // 3028 ラヂオななみ
    0x0000000000000000ULL,  // 3029 ＦＭ ＯＺＥ
    0x0000000000000000ULL,  // 3030 ラジオ川越
    0x0000000000000000ULL,  // 3031 発するFM
    0x0000000000000000ULL,  // 3032 ＦＭチャッピー
    0x0000000000000000ULL,  // 3033 FMふくろう
    0x0000000000000000ULL,  // 3034 SKYWAVE FM
    0x0000000000000000ULL,  // 3035 かずさFM
    0x0000000000000000ULL,  // 3036 ラジオ成田
    0x0000000000000000ULL,  // 3037 市川うららFM(I&U-LaLaFM)
    0x0000000000000000ULL,  // 3038 FMしながわ
    0x0000000000000000ULL,  // 3039 かつしかFM
    0x0000000000000000ULL,  // 3040 むさしのＦＭ
    0x0000000000000000ULL,  // 3041 コマラジ
    0x0000000000000000ULL,  // 3042 中央エフエム・RADIO CITY
    0x0000000000000000ULL,  // 3043 渋谷のラジオ
    0x0000000000000000ULL,  // 3044 ＦＭえどがわ
    0x0000000000000000ULL,  // 3045 FMおだわら
    0x0000000000000000ULL,  // 3046 FMやまと
    0x0000000000000000ULL,  // 3047 FMサルース
    0x0000000000000000ULL,  // 3048 FM湘南ナパサ
    0x0000000000000000ULL,  // 3049 FM湘南マジックウェイブ
    0x0000000000000000ULL,  // 3050 エフエム戸塚
    0x0000000000000000ULL,  // 3051 エボラジ
    0x0000000000000000ULL,  // 3052 マリンFM
    0x0000000000000000ULL,  // 3053 レディオ湘南
    0x0000000000000000ULL,  // 3054 鎌倉FM
    0x0000000000000000ULL,  // 3055 関東臨時災害放送局訓練
    0x0000000000000000ULL,  // 3056 ＦＭブルー湘南
    0x0000000000000000ULL,  // 3057 FM KENTO
    0x0000000000000000ULL,  // 3058 FMうおぬま
    0x0000000000000000ULL,  // 3059 FMじょうえつ
    0x0000000000000000ULL,  // 3060 FMピッカラ
    0x0000000000000000ULL,  // 3061 エフエムしばた
    0x0000000000000000ULL,  // 3062 エフエムながおか
    0x0000000000000000ULL,  // 3063 ラジオチャット・FMにいつ
    0x0000000000000000ULL,  // 3064 ＦＭゆきぐに
    0x0000000000000000ULL,  // 3065 エフエムとなみ
    0x0000000000000000ULL,  // 3066 ラジオたかおか
    0x0000000000000000ULL,  // 3067 富山シティエフエム株式会社
    0x0000000000000000ULL,  // 3068 ラジオかなざわ
    0x0000000000000000ULL,  // 3069 ラジオこまつ
    0x0000000000000000ULL,  // 3070 ラジオななお
    0x0000000000000000ULL,  // 3071 FMふじやま
    0x0000000000000000ULL,  // 3072 FM八ヶ岳
    0x0000000000000000ULL,  // 3073 エフエム　ふじごこ
    0x0000000000000000ULL,  // 3074 エフエム甲府
    0x0000000000000000ULL,  // 3075 FM軽井沢
    0x0000000000000000ULL,  // 3076 LCV FM
    0x0000000000000000ULL,  // 3077 伊那谷FM
    0x0000000000000000ULL,  // 3078 高ボッチ高原FM
    0x0000000000000000ULL,  // 3079 ＦＭわっち
    0x0000000000000000ULL,  // 3080 ＦＭＰｉＰｉ
    0x0000000000000000ULL,  // 3081 Ｈｉｔｓ ＦＭ
    0x0000000000000000ULL,  // 3082 COAST-FM76.7MH z
    0x0000000000000000ULL,  // 3083 Ciao!
    0x0000000000000000ULL,  // 3084 FM Haro!
    0x0000000000000000ULL,  // 3085 FM ISみらいずステーション
    0x0000000000000000ULL,  // 3086 FMいずのくに
    0x0000000000000000ULL,  // 3087 RADIO LUSH
    0x0000000000000000ULL,  // 3088 Radio-f
    0x0000000000000000ULL,  // 3089 S-Wave
    0x0000000000000000ULL,  // 3090 g-sky76.5
    0x0000000000000000ULL,  // 3091 エフエムなぎさステーション
    0x0000000000000000ULL,  // 3092 ボイスキュー
    0x0000000000000000ULL,  // 3093 富士山ＧＯＧＯＦＭ
    0x0000000000000000ULL,  // 3094 HeartFM
    0x0000000000000000ULL,  // 3095 RADIO SANQ
    0x0000000000000000ULL,  // 3096 United North
    0x0000000000000000ULL,  // 3097 i-wave
    0x0000000000000000ULL,  // 3098 エフエム　ななみ
    0x0000000000000000ULL,  // 3099 Suzuka Voice FM 78.3MHz
    0x0000000000000000ULL,  // 3100 いなべエフエム
    0x0000000000000000ULL,  // 3101 えふえむ草津
    0x0000000000000000ULL,  // 3102 FM845
    0x0000000000000000ULL,  // 3103 FMうじ
    0x0000000000000000ULL,  // 3104 FMまいづる
    0x0000000000000000ULL,  // 3105 ＦＭいかる
    0x0000000000000000ULL,  // 3106 FM千里
    0x0000000000000000ULL,  // 3107 ウメダFM Be Happy!789
    0x0000000000000000ULL,  // 3108 タッキー816みのおエフエム
    0x0000000000000000ULL,  // 3109 805たんば
    0x0000000000000000ULL,  // 3110 BAN-BANラジオ
    0x0000000000000000ULL,  // 3111 FM GENKI
    0x0000000000000000ULL,  // 3112 さくらFM
    0x0000000000000000ULL,  // 3113 エフエムいたみ
    0x0000000000000000ULL,  // 3114 エフエムみっきぃ
    0x0000000000000000ULL,  // 3115 ハミングFM宝塚
    0x0000000000000000ULL,  // 3116 FMまほろば
    0x0000000000000000ULL,  // 3117 なら どっと ＦＭ
    0x0000000000000000ULL,  // 3118 ＦＭ五條
    0x0000000000000000ULL,  // 3119 FM TANABE
    0x0000000000000000ULL,  // 3120 FMはしもと
    0x0000000000000000ULL,  // 3121 FMビーチステーション
    0x0000000000000000ULL,  // 3122 FM新宮
    0x0000000000000000ULL,  // 3123 バナナエフエム
    0x0000000000000000ULL,  // 3124 RADIO BIRD
    0x0000000000000000ULL,  // 3125 FMくらしき
    0x0000000000000000ULL,  // 3126 レディオ モモ
    0x0000000000000000ULL,  // 3127 FMおのみち
    0x0000000000000000ULL,  // 3128 FMはつかいち
    0x0000000000000000ULL,  // 3129 FMふくやま
    0x0000000000000000ULL,  // 3130 FM東広島
    0x0000000000000000ULL,  // 3131 FOR LIFE RADIO
    0x0000000000000000ULL,  // 3132 ＦＭミコリン
    0x0000000000000000ULL,  // 3133 しゅうなんＦＭ
    0x0000000000000000ULL,  // 3134 ＣＯＭＥ ＯＮ ! ＦＭ
    0x0000000000000000ULL,  // 3135 エフエム・サン
    0x0000000000000000ULL,  // 3136 FMがいや
    0x0000000000000000ULL,  // 3137 FMラヂオバリバリ
    0x0000000000000000ULL,  // 3138 Hello! NEW 新居浜 FM
    0x0000000000000000ULL,  // 3139 Dreams FM
    0x0000000000000000ULL,  // 3140 FM八女
    0x0000000000000000ULL,  // 3141 ＦＭからつ
    0x0000000000000000ULL,  // 3142 FM791
    0x0000000000000000ULL,  // 3143 Kappa FM
    0x0000000000000000ULL,  // 3144 NOASFM
    0x0000000000000000ULL,  // 3145 ゆふいんラヂオ局
    // ==== 4: ListenRadio ====
    0x0000000000000000ULL,  // 4001 FM ABASHIRI
    0x0000000000000000ULL,  // 4002 FM G'Sky
    0x0000000000000000ULL,  // 4003 FM JAGA
    0x0000000000000000ULL,  // 4004 FM WING
    0x0000000000000000ULL,  // 4005 FMおたる
    0x0000000000000000ULL,  // 4006 FMくしろ
    0x0000000000000000ULL,  // 4007 FMねむろ
    0x0000000000000000ULL,  // 4008 FMはまなす
    0x0000000000000000ULL,  // 4009 FMわっぴー
    0x0000000000000000ULL,  // 4010 FMアップル
    0x0000000000000000ULL,  // 4011 FMメイプルきたひろボールパークラジオ
    0x0000000000000000ULL,  // 4012 RadioYAMASHO FMドラマシティ
    0x0000000000000000ULL,  // 4013 wi-radio
    0x0000000000000000ULL,  // 4014 さっぽろ村ラジオ
    0x0000000000000000ULL,  // 4015 ラジオカロスサッポロ
    0x0000000000000000ULL,  // 4016 三角山放送局（札幌市西区）
    0x0000000000000000ULL,  // 4017 BeFM
    0x0000000000000000ULL,  // 4018 みやこハーバーラジオ
    0x0000000000000000ULL,  // 4019 カシオペアFM
    0x0000000000000000ULL,  // 4020 ラヂオもりおか
    0x0000000000000000ULL,  // 4021 BAY WAVE
    0x0000000000000000ULL,  // 4022 RADIO3
    0x0000000000000000ULL,  // 4023 fmいずみ
    0x0000000000000000ULL,  // 4024 なとらじ801
    0x0000000000000000ULL,  // 4025 ラジオ石巻
    0x0000000000000000ULL,  // 4026 ラヂオ気仙沼
    0x0000000000000000ULL,  // 4027 FMゆーとぴあ
    0x0000000000000000ULL,  // 4028 エフエム椿台
    0x0000000000000000ULL,  // 4029 横手かまくらエフエム
    0x0000000000000000ULL,  // 4030 鹿角きりたんぽFM
    0x0000000000000000ULL,  // 4031 FM Mot.com
    0x0000000000000000ULL,  // 4032 FMいわき
    0x0000000000000000ULL,  // 4033 KOCOラジ
    0x0000000000000000ULL,  // 4034 FMうしくうれしく放送
    0x0000000000000000ULL,  // 4035 FMぱるるん
    0x0000000000000000ULL,  // 4036 FMひたち
    0x0000000000000000ULL,  // 4037 まえばしラジオ
    0x0000000000000000ULL,  // 4038 775ライブリーFM
    0x0000000000000000ULL,  // 4039 FM Kawaguchi
    0x0000000000000000ULL,  // 4040 FMふっかちゃん
    0x0000000000000000ULL,  // 4041 FMわたらせ
    0x0000000000000000ULL,  // 4042 REDS WAVE
    0x0000000000000000ULL,  // 4043 ハローハッピー・こしがやエフエム
    0x0000000000000000ULL,  // 4044 フラワーラジオ
    0x0000000000000000ULL,  // 4045 Cwave
    0x0000000000000000ULL,  // 4046 FMたちかわ
    0x0000000000000000ULL,  // 4047 Tokyo Star Radio(八王子FM)
    0x0000000000000000ULL,  // 4048 エフエム世田谷
    0x0000000000000000ULL,  // 4049 レインボータウンFM
    0x0000000000000000ULL,  // 4050 調布FM
    0x0000000000000000ULL,  // 4051 ＦＭカオン
    0x0000000000000000ULL,  // 4052 ラジオ・ミュー
    0x0000000000000000ULL,  // 4053 FM N1
    0x0000000000000000ULL,  // 4054 敦賀FM
    0x0000000000000000ULL,  // 4055 エフエムEGAO
    0x0000000000000000ULL,  // 4056 エフエム花
    0x0000000000000000ULL,  // 4057 ラジオスイート
    0x0000000000000000ULL,  // 4058 FM87.0 RADIO MIX KYOTO
    0x0000000000000000ULL,  // 4059 FMおとくに
    0x0000000000000000ULL,  // 4060 FMたんご
    0x0000000000000000ULL,  // 4061 京都三条ラジオカフェ
    0x0000000000000000ULL,  // 4062 BAN-BANラジオ
    0x0000000000000000ULL,  // 4063 FM ジャングル
    0x0000000000000000ULL,  // 4064 DARAZ　FM
    0x0000000000000000ULL,  // 4065 FMびざん
    0x0000000000000000ULL,  // 4066 FM815（高松）
    0x0000000000000000ULL,  // 4067 AIR STATION HIBIKI
    0x0000000000000000ULL,  // 4068 チョクラジ
    0x0000000000000000ULL,  // 4069 FMのべおか
    0x0000000000000000ULL,  // 4070 あまみエフエム
    0x0000000000000000ULL,  // 4071 エフエムたつごう
    0x0000000000000000ULL,  // 4072 FMぎのわん
    0x0000000000000000ULL,  // 4073 FMとよみ
    0x0000000000000000ULL,  // 4074 FMやんばる
    0x0000000000000000ULL,  // 4075 FMよなばる
    0x0000000000000000ULL,  // 4076 fm那覇
    0x0000000000000000ULL,  // 4077 ぎのわんシティFM
    0x0000000000000000ULL,  // 4078 オキラジ
    // ==== 5: FMぷらぷら ====
    0x0000000000000000ULL,  // 5001 ラジオカロスサッポロ
    0x0000000000000000ULL,  // 5002 FMあすも
    0x0000000000000000ULL,  // 5003 きたかみE&Beエフエム
    0x0000000000000000ULL,  // 5004 ＯＣＲFM835
    0x0000000000000000ULL,  // 5005 FMはなび
    0x0000000000000000ULL,  // 5006 FM DAMONO
    0x0000000000000000ULL,  // 5007 FMくらら857
    0x0000000000000000ULL,  // 5008 FMもおか
    0x0000000000000000ULL,  // 5009 FMゆうがお
    0x0000000000000000ULL,  // 5010 おーラジ
    0x0000000000000000ULL,  // 5011 ミヤラジ
    0x0000000000000000ULL,  // 5012 FM桐生
    0x0000000000000000ULL,  // 5013 FMクマガヤ
    0x0000000000000000ULL,  // 5014 あいラジ
    0x0000000000000000ULL,  // 5015 ちちぶFM
    0x0000000000000000ULL,  // 5016 Hello FM
    0x0000000000000000ULL,  // 5017 TOKYO854 くるめラ
    0x0000000000000000ULL,  // 5018 エフエム西東京
    0x0000000000000000ULL,  // 5019 ラジオTAMAリバー
    0x0000000000000000ULL,  // 5020 FM大師
    0x0000000000000000ULL,  // 5021 かわさきエフエム
    0x0000000000000000ULL,  // 5022 金沢シーサイドFM
    0x0000000000000000ULL,  // 5023 FMとおかまち
    0x0000000000000000ULL,  // 5024 FMながおか
    0x0000000000000000ULL,  // 5025 FMみょうこう
    0x0000000000000000ULL,  // 5026 ラヂオは〜と
    0x0000000000000000ULL,  // 5027 まちのラジオ(災害FM)
    0x0000000000000000ULL,  // 5028 FMぜんこうじ
    0x0000000000000000ULL,  // 5029 FMまつもと
    0x0000000000000000ULL,  // 5030 fmさくだいら
    0x0000000000000000ULL,  // 5031 あづみ野エフエム
    0x0000000000000000ULL,  // 5032 いいだFM
    0x0000000000000000ULL,  // 5033 はれラジ
    0x0000000000000000ULL,  // 5034 FMらら76.8
    0x0000000000000000ULL,  // 5035 KATCH&Pitch 地域情報
    0x0000000000000000ULL,  // 5036 TEES-843FM
    0x0000000000000000ULL,  // 5037 メディアスFM
    0x0000000000000000ULL,  // 5038 ラジオ・ラブィート
    0x0000000000000000ULL,  // 5039 名古屋市防災(MID-FM)
    0x0000000000000000ULL,  // 5040 CTY-FM
    0x0000000000000000ULL,  // 5041 FMおおつ
    0x0000000000000000ULL,  // 5042 びわこの東のFM
    0x0000000000000000ULL,  // 5043 FMいずみおおつ
    0x0000000000000000ULL,  // 5044 YES-fm
    0x0000000000000000ULL,  // 5045 やおFM
    0x0000000000000000ULL,  // 5046 奥河内ラジオ
    0x0000000000000000ULL,  // 5047 FM BLOOM(あま咲き放送局)
    0x0000000000000000ULL,  // 5048 FMハイホー
    0x0000000000000000ULL,  // 5049 FMヤマト
    0x0000000000000000ULL,  // 5050 ゆめウェーブ
    0x0000000000000000ULL,  // 5051 FMわっしょい
    0x0000000000000000ULL,  // 5052 FMスマイルウェ～ブ
    0x0000000000000000ULL,  // 5053 FMはたらんど
    0x0000000000000000ULL,  // 5054 COMIxTEN
    0x0000000000000000ULL,  // 5055 FM KITAQ
    0x0000000000000000ULL,  // 5056 FMたんと
    0x0000000000000000ULL,  // 5057 えびすFM
    0x0000000000000000ULL,  // 5058 FMさせぼ
    0x0000000000000000ULL,  // 5059 FMしまばら
    0x0000000000000000ULL,  // 5060 FMひまわり
    0x0000000000000000ULL,  // 5061 Silk FM
    0x0000000000000000ULL,  // 5062 エフエム対馬
    0x0000000000000000ULL,  // 5063 エフエム諫早
    0x0000000000000000ULL,  // 5064 みつばちラジオ
    0x0000000000000000ULL,  // 5065 エフエムきつき802
    0x0000000000000000ULL,  // 5066 サンシャインFM
    0x0000000000000000ULL,  // 5067 FMかのや
    0x0000000000000000ULL,  // 5068 FMきりしま
    0x0000000000000000ULL,  // 5069 FMさつませんだい
    0x0000000000000000ULL,  // 5070 フレンズFM
    0x0000000000000000ULL,  // 5071 FMいしがきサンサンラジオ
    0x0000000000000000ULL,  // 5072 FMうるま
    0x0000000000000000ULL,  // 5073 FMみやこ
};

static_assert(sizeof(kStationFreeAreaMask) / sizeof(kStationFreeAreaMask[0]) == kStationCount,
              "kStationFreeAreaMask length must match kStations");

// エリア番号(JP番号 = 都道府県のJISコード)から表示用の都道府県名を引く。添字は1始まり。
constexpr const char* kAreaNames[] = {
    "",
    "北海道",
    "青森",
    "岩手",
    "宮城",
    "秋田",
    "山形",
    "福島",
    "茨城",
    "栃木",
    "群馬",
    "埼玉",
    "千葉",
    "東京",
    "神奈川",
    "新潟",
    "富山",
    "石川",
    "福井",
    "山梨",
    "長野",
    "岐阜",
    "静岡",
    "愛知",
    "三重",
    "滋賀",
    "京都",
    "大阪",
    "兵庫",
    "奈良",
    "和歌山",
    "鳥取",
    "島根",
    "岡山",
    "広島",
    "山口",
    "徳島",
    "香川",
    "愛媛",
    "高知",
    "福岡",
    "佐賀",
    "長崎",
    "熊本",
    "大分",
    "宮崎",
    "鹿児島",
    "沖縄",
};

constexpr size_t kAreaNameCount = sizeof(kAreaNames) / sizeof(kAreaNames[0]);
