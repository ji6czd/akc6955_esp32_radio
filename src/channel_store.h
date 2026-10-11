#pragma once

// 再生中チャンネルの不揮発保存(Preferences/NVS)を担当する。volume_store.hと同じく、
// 変更の都度は書き込まず、一定時間チャンネル切り替えが無かった場合にのみ実際に
// 書き込む(デバウンス)。
//
// 保存するのはg_channelListのインデックス(kStationsの添字。直接選局コードではない)。
// 局一覧はコンパイル時の固定テーブルなので起動ごとにずれることは無いが、テーブルを
// 再生成したファームへ書き換えると添字の意味が変わりうる。その検出のために局数も
// 一緒に保存し、違っていればデフォルトへ落とす(channel_store.cpp参照)。

// 起動時、g_channelListを組み立てた後に呼ぶ。保存済みのインデックスがあればそれを、
// 無ければdefaultIndexを返す。
int channelStoreLoadIndex(int defaultIndex);

// チャンネルが切り替わるたびに呼ぶ。この時点では書き込まない。
// 実際の書き込みはchannelStoreTick()側で行う。
void channelStoreNotifyChanged(int index);

// loop()から毎回呼ぶ。デバウンス期間(config::kChannelSaveDebounceMs)経過後、
// 変更が溜まっていれば実際にPreferencesへ書き込む。
void channelStoreTick();
