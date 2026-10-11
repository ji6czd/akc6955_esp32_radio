#pragma once

#include "mode.h"

// インターネットラジオ(radiko / NHK など)モード。
// 初回enter()でradioStreamBegin()し、以降のenter()/exit()はResume/Stopで切り替える。
// テンキーによる直接選局・エリア/プラットフォーム送り・タイムフリーのシークなどを担当する。
Mode& netRadioMode();
