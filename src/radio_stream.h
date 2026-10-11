#pragma once

#include <Arduino.h>

// HLSライブストリームの再生を統括する。
// プレイリストの追跡・鍵の取得・セグメントのダウンロード/復号(fetchTaskFn)と、
// AACデコード(decodeTaskFn)はそれぞれ専用タスクで行い、バイトのリングバッファ経由で
// 疎結合にしている。デコード後のPCM→I2S書き込みも同様にi2s_speaker.cpp側の専用タスクへ
// リングバッファ経由で渡す。これによりネットワークの瞬断・プレイリスト再取得の待ち・
// I2S書き込みの実時間ペースのブロックが、Arduinoのloop()(キー入力処理)に一切影響しない。
// このアプリではインスタンスは1つだけなので、フリー関数として提供する。

// g_channelList(channel_list.h)のinitialChannelIndex番目からメディアプレイリスト・
// AES鍵を取得し、ライブエッジ付近から再生キューを組み立てたうえで、ネットワーク取得
// タスク(fetchTaskFn)とデコードタスク(decodeTaskFn)を起動する
// (ここまではブロッキングでリトライする)。
// g_channelListは呼び出し前に(nhkFetchChannels()等で)入っている必要がある。
void radioStreamBegin(int initialChannelIndex);

// radioStreamBegin()が使う大きなバッファを先に確保する。PSRAMの無いESP32ではWi-Fi/TLSが
// ヒープを使い始めた後だと連続領域が取れないので、Wi-Fi接続より前に呼ぶこと。
// 呼ばなくてもradioStreamBegin()が確保するが、その時点で足りなければ再生を始められない。
bool radioStreamReserveMemory();

// ネットワーク→デコード間バッファに溜まっている(未デコードの)バイト数。診断ログ用。
// 再生開始前は0。どのタスクからでも呼べる。
size_t radioStreamBufferedBytes();

// g_channelListの指定インデックスへ直接切り替える(範囲外は無視)。テンキー直接入力等、
// 絶対インデックスが分かっている場合はこちらを使う。
// どのコア/タスクから呼んでもよい(内部でスレッドセーフなキューを介して
// ネットワーク取得タスクへ伝える)。
// 戻り値: 切り替え要求を受け付けたら true。範囲外や、無料利用で在圏外
// (プレミアム専用)の局を指定した場合は false(現在の再生を維持する)。
bool radioStreamSetChannel(int index);

// g_channelListの「現在再生中のチャンネル」を基準に次/前へ切り替える(末尾/先頭で回り込む)。
// 内部的にはradioStreamSetChannel()を呼ぶだけ。
// 戻り値は切り替え先の(このあと再生されるべき)チャンネルインデックス。切り替えは非同期
// なので、呼び出し直後にg_channelList.getCurrentIndex()を見てもまだ更新されていない
// 点に注意(この戻り値で切り替え先を知る)。局リストは静的で必ず1局以上あるため、
// 常に有効なインデックスを返す(失敗のケースは無い)。
int radioStreamNextChannel();
int radioStreamPreviousChannel();

// 同じプラットフォーム内で次/前のエリアの先頭局へ切り替える(プラットフォームの
// 末尾/先頭で同プラットフォーム内へ回り込む)。全国300局超を1局ずつ送るのは現実的では
// ないため、エリア単位の粗い移動としてfuncA/funcBに割り当てている。
// 次/前のプラットフォームの先頭局へ切り替えるのがradioStreamNext/PreviousPlatform()
// (funcC/funcD)。いずれも、先頭局が無料利用で在圏外(プレミアム専用)のradiko局なら
// next/prevと同様にそれを飛ばして、その先の最寄りの再生可能局へ切り替える。
// 戻り値は切り替え先のチャンネルインデックス(radioStreamNextChannel()と同じ注意)。
int radioStreamNextArea();
int radioStreamPreviousArea();
int radioStreamNextPlatform();
int radioStreamPreviousPlatform();

// タイムフリー(過去番組)のシーク。現在の再生位置(ライブ再生中ならその時点の現在時刻)から
// deltaSec 秒ずらした位置へ、同じ局のまま再生し直す。負で過去方向、正で未来方向。
// 遡り限界(約30日)・直近端(現在の約2分前まで)は内部でクランプし、現在時刻に十分近づいたら
// 自動的にライブ再生へ戻る。radikoチャンネルかつシステム時刻が同期済みのときだけ有効。
// どのコア/タスクから呼んでもよい(選局と同じスレッドセーフなキューを介する)。
void radioStreamSeekRelative(int deltaSec);

// 現在の局のタイムフリー再生を、指定した絶対的な壁時計時刻(UNIX時間)へ飛ばす。
// 相対シーク(radioStreamSeekRelative)と違い、現在の再生位置を基準にしない。
// 目標は内部でタイムフリー到達可能ウィンドウ(約30日前〜直近端)へクランプされる。
// radikoチャンネルかつシステム時刻が同期済みのときだけ有効(それ以外は何もせず false)。
// どのコア/タスクから呼んでもよい(選局と同じスレッドセーフなキューを介する)。
bool radioStreamSeekAbsolute(time_t targetEpoch);

// now を基準にしたタイムフリー到達可能ウィンドウ [oldest, newest] を返す。
// radioStreamSeekAbsolute() へ渡す前の範囲チェックに使う(内部のクランプと同じ境界)。
void radioStreamTimefreeWindow(time_t now, time_t* oldest, time_t* newest);

// タイムフリー再生を打ち切ってライブ再生へ戻す。ライブ再生中に呼んでも無害。
void radioStreamReturnToLive();

// 通知音(モールス符号など)を鳴らすあいだ、ラジオのデコード→I2S供給を一時停止する。
// i2s_speakerは常に1本のストリームだけを流す前提(単一プロデューサ)なので、通知音を
// 鳴らす側が呼び出して、その間だけ唯一の書き込み元になれるようにする。
// Pause側はデコードタスクが供給を止めたことを確認してから戻る(数十ms)。停止中、
// ネットワーク取得タスクはバッファが埋まるとそこで待つ(音声データは失わない)。
// 停止が数秒に及ぶと復帰後にライブエッジへ再同期することがあるので、通知は短く保つこと。
// 必ず対で呼ぶこと(Resumeを忘れると無音のままになる)。
void radioStreamPauseForNotification();
void radioStreamResumeAfterNotification();

// 再生を停止し、解放できるリソース(共有TLS接続・JCBAのWebSocket・AAC/OpusデコーダのHelix/
// Opusワークバッファ)をすべて解放する。radioStreamPauseForNotification()と違って
// Wi-Fi接続以外はほぼ何も維持しないので、通知音のような短時間の一時停止ではなく、
// しばらく再生しない(番組の合間、就寝前など)場合に使う想定。
// Wi-Fi接続自体は維持する(切断すると再接続に数秒かかるため)。I2S出力は呼び出し直後に
// 止まる。選局中の局(g_channelList.getCurrentIndex())は変更しない。
// どのコア/タスクから呼んでもよい。
void radioStreamStop();

// radioStreamStop()の後、直前に選局していた局へ選局し直す形で再生を再開する
// (プレイリスト取得・認証等をすべてやり直す)。タイムフリー再生中にradioStreamStop()を
// 呼んだ場合、これで戻るのはライブ(タイムフリー位置は保持しない)。
// 停止中でなくても無害(同一局への選局し直しになるだけ)。どのコア/タスクから呼んでもよい。
void radioStreamResume();
