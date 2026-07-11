#pragma once
// mdx_engine.hpp
// MDX/PDXファイル再生エンジン（voice統合ラッパー）
// vgm_engine.hと同様のインターフェースを提供する
//
// 内部でvoice側のfm_engine(YM2151モード)とpcm_engineを使用する

#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

// MDXエンジン初期化（setup()から呼ぶ）
void mdx_engine_init(void);

// MDXファイルを再生開始する（SDカードから読み込み）
// mdx_path: MDXファイルのフルパス（例: "/MDX/VOCALOID.MDX"）
// 戻り値: true=成功, false=失敗
bool mdx_engine_play(const char *mdx_path);

// 再生を停止する
void mdx_engine_stop(void);

// 再生中かどうかを返す
bool mdx_engine_is_playing(void);

// 現在再生中のタイトル文字列を返す（SJIS）
// 再生していない場合はNULLを返す
const char *mdx_engine_get_title(void);

// 最後のエラーメッセージを返す
const char *mdx_engine_get_error(void);

#ifdef __cplusplus
}
#endif
