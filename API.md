# GCMZDrops 外部連携 API リファレンス

このドキュメントでは、GCMZDrops の外部連携 API を使用して、別のアプリケーションから AviUtl ExEdit2 のタイムラインへファイルをドロップする方法を説明します。

## 目次

- [概要](#概要)
- [注意事項](#注意事項)
- [Mutex について](#mutex-について)
- [FileMappingObject について](#filemappingobject-について)
- [ファイルドロップ API](#ファイルドロップ-api)
- [実装例](#実装例)
- [変更履歴](#変更履歴)

## 概要

GCMZDrops 外部連携 API は、以下の仕組み利用して実現しています。

| 名前 | 用途 |
|------|------|
| Mutex | 排他制御 |
| FileMappingObject | プロジェクト情報の取得 |
| WM_COPYDATA メッセージ | ファイルドロップリクエストの送信に使用 |

外部アプリケーションは Mutex で排他制御を行い、FileMappingObject からプロジェクト情報を取得し、SendMessageW で `WM_COPYDATA` メッセージを送信することでファイルをタイムラインにドロップできます。

個々の説明ではなく全体像を把握したい場合は [使用例](#使用例) セクションを参照してください。

## 注意事項

外部連携 API が使えるのは最初に起動したインスタンスのみです。

AviUtl ExEdit2 を多重起動しても、2 つ目以降では API は無効状態になります。

## Mutex について

GCMZDrops は外部連携 API が有効な場合、 `GCMZDropsMutex` という名前の Mutex を作成します。  
外部アプリケーションはこの Mutex を開き、FileMappingObject からの読み込みの際に排他制御を行う必要があります。

また、API が無効な場合は Mutex が存在しないため、OpenMutexW に失敗します。  
これを利用して、外部連携 API が利用可能かどうかを判定できます。

## FileMappingObject について

GCMZDrops は外部連携 API が有効な場合、 `GCMZDrops` という名前の FileMappingObject を作成します。  
外部アプリケーションはこの FileMappingObject を開き、プロジェクト情報を取得できます。

読み取る際は、Mutex を使用して排他制御を行うようにしてください。

### データ構造

FileMappingObject には以下の `GCMZDropsData` 構造体が格納されています。  
過去のバージョンでは定義されていないフィールドもあるため、`GCMZAPIVer` フィールドの値を確認してから使用してください。

```c
struct GCMZDropsData {
  uint32_t Window;
  int32_t Width;
  int32_t Height;
  int32_t VideoRate;
  int32_t VideoScale;
  int32_t AudioRate;
  int32_t AudioCh;
  int32_t GCMZAPIVer;
  wchar_t ProjectPath[260];
  uint32_t Flags;
  uint32_t AviUtlVer;
  uint32_t GCMZVer;
};
```

| フィールド    | タイプ       | GCMZAPIVer | 説明 |
|---------------|--------------|------------|------|
| `Window`      | uint32_t     | 0          | ファイルドロップリクエストを送信する先のウィンドウハンドル |
| `Width`       | int32_t      | 0          | 動画の幅（ピクセル）。`0` の場合はプロジェクトが開かれていない |
| `Height`      | int32_t      | 0          | 動画の高さ（ピクセル） |
| `VideoRate`   | int32_t      | 0          | フレームレートの分子 |
| `VideoScale`  | int32_t      | 0          | フレームレートの分母 |
| `AudioRate`   | int32_t      | 0          | 音声サンプリングレート（Hz） |
| `AudioCh`     | int32_t      | 0          | 音声チャンネル数。詳細は後述。 |
| `GCMZAPIVer`  | int32_t      | 1          | API バージョン番号。詳細は後述。 |
| `ProjectPath` | wchar_t[260] | 1          | プロジェクトファイルへのフルパス。詳細は後述。 |
| `Flags`       | uint32_t     | 2          | 各種フラグ。詳細は後述。 |
| `AviUtlVer`   | uint32_t     | 3          | AviUtl ExEdit2 のバージョン番号。詳細は後述。 |
| `GCMZVer`     | uint32_t     | 3          | GCMZDrops のバージョン番号。詳細は後述。 |

#### AudioCh フィールド

`AudioCh` フィールドはプロジェクトの音声チャンネル数を表します。

ただし AviUtl ExEdit2 からプロジェクトのオーディオチャンネル数の情報は提供されていないため、現在は常に `2` が割り当てられています。

#### GCMZAPIVer フィールド

`GCMZAPIVer` フィールドは `GCMZAPIVer == 1` 以上の環境で利用可能です。

これは API バージョン番号を表し、API 仕様の変更時に増加します。  
ただし `0` の時点では `GCMZAPIVer` フィールド自体が存在しないため、外部アプリケーションからは検出できません。

| GCMZAPIVer | リリース   | バージョン        | 備考 |
|------------|------------|-------------------|------|
| 0          | 2018-04-08 | GCMZDrops v0.3    | `GCMZAPIVer` フィールドが存在しないため検出不可 |
| 1          | 2020-06-25 | GCMZDrops v0.3.12 | `GCMZAPIVer` と `ProjectPath` フィールドを追加、ファイルドロップ API の dwData = 1 に対応 |
| 2          | 2021-08-02 | GCMZDrops v0.3.23 | `Flags` フィールドを追加 |
| 3          | 2025-11-24 | GCMZDrops v2.0    | `AviUtlVer` と `GCMZVer` フィールドを追加、ファイルドロップ API の dwData = 2 に対応 |

#### ProjectPath フィールド

`ProjectPath` フィールドは `GCMZAPIVer == 1` 以上の環境で利用可能です。

`ProjectPath` フィールドには、現在開かれているプロジェクトファイルのフルパスが格納されています。  
プロジェクトが未保存の場合は空文字列（1文字目が `\0`）になります。

Windows では `sizeof(wchar_t) == 2` であり、長さの `260` は `MAX_PATH` と同じ長さです。  
実際のプロジェクトパスが `MAX_PATH` を超えている場合、259文字と終端文字に切り詰められた状態で格納されます。

#### Flags フィールド

`Flags` フィールドは `GCMZAPIVer == 2` 以上の環境で利用可能です。

AviUtl1 で本体に翻訳パッチが適用されているかどうかを示す目的でのみ使われていました。  
翻訳パッチが適用されている環境においては作成すべき *.exo の仕様が異なるため、外部アプリケーションが適切に対応できるようにするため二追加されました。  
AviUtl ExEdit2 ではパッチなしで多言語対応が行われているため、このフィールは使用されません。

| ビット     | 説明 |
|------------|------|
| 0x00000001 | 英語翻訳パッチが適用されている（AviUtl1 用） |
| 0x00000002 | 簡体字中国語翻訳パッチが適用されている（AviUtl1 用） |

#### AviUtlVer フィールド / GCMZVer フィールド

`AviUtlVer` および `GCMZVer` フィールドは `GCMZAPIVer == 3` 以上の環境で利用可能です。

`AviUtlVer` には AviUtl ExEdit2 のバージョン、`GCMZVer` には GCMZDrops のバージョンが格納されています。  
どちらも整数値ですが、バージョン番号の表現方法が異なります。

AviUtl ExEdit2 version 2.0beta24a は `2002401` として表されます。  
GCMZDrops v2.0.0alpha9 は `67108873` として表されます。

GCMZDrops のバージョンは値をビット単位で割り当てているため直感的ではありませんが、どちらのフィールドも新しいリリースほど大きな値になります。  
もしバグ回避などの目的でバージョン判定を行いたい場合は、単純に数値の大小を比較することで、ある時期のリリースより新しいかどうかを判定できます。

## ファイルドロップ API

AviUtl ExEdit2 のタイムラインへファイルをドロップするには、`WM_COPYDATA` メッセージを SendMessageW 関数で送信します。

```c
char const json[] = "...";
COPYDATASTRUCT cds = {
  .dwData = 2,
  .cbData = strlen(json),
  .lpData = json,
};
SendMessageW(window, WM_COPYDATA, (WPARAM)sender_window, (LPARAM)&cds);
```

### COPYDATASTRUCT の構成

| フィールド | 説明 |
|-----------|------|
| `dwData` | データフォーマット識別子 |
| `cbData` | データサイズ（バイト） |
| `lpData` | データへのポインタ |

#### dwData フィールド

GCMZDrops へファイルドロップを行う際の、データフォーマットを指定します。  
FileMappingObject の `GCMZAPIVer` フィールドの値に応じて、利用可能な値が異なります。

| 値 | GCMZAPIVer | フォーマット | 備考 |
|----|------------|--------------|------|
| 0  | 0          | レガシー(wchar_t) | 非推奨。後方互換性のためのみ |
| 1  | 1 or 2     | JSON(UTF-8) | 主に AviUtl1 用。AviUtl ExEdit2 では簡易的な `*.exo` の自動変換を試みる |
| 2  | 3          | JSON(UTF-8) | `layer = 0` で選択中のレイヤーへの投げ込みが可能、`margin` パラメーターが使用可能 |

AviUtl ExEdit2 ではタイムラインへの投げ込みに `*.exo` ファイルはサポートされていません。  
`dwData = 0` か `dwData = 1` を使用すると GCMZDrops が `*.exo` ファイルを `*.object` に自動変換を試みますが、これは簡易的なもので多くの場合に対応できません。  
最初から `*.object` 形式を使用してください。

#### cbData フィールド

`cbData` フィールドには、`lpData` フィールドで指定したデータのサイズ（バイト単位）を指定します。

#### lpData フィールド

`lpData` フィールドには、`dwData` フィールドで指定したフォーマットのデータへのポインタを指定します。

### JSON フォーマット

`dwData = 2` で使用する JSON フォーマットです。  
`dwData = 1` の場合は一部に使えない機能があることに注意してください。

```json
{
  "layer": 1,
  "frameAdvance": 100,
  "margin": 10,
  "files": [
    "C:\\Path\\To\\File1.png",
    "C:\\Path\\To\\File2.wav"
  ]
}
```

| フィールド | 型 | 説明 |
|-----------|---|-----|------|
| `layer` | integer | ドロップ先のレイヤー番号。省略時は `0` |
| `frameAdvance` | integer | ドロップ後にカーソル位置を何フレーム進めるか。省略時は `0` |
| `margin` | integer | 挿入先にオブジェクトが存在する場合の処理方法。省略時は `-1` |
| `files` | array | ドロップするファイルのフルパス（UTF-8）の配列。空配列は不可 |

#### layer フィールド

`layer` フィールドはドロップ先のレイヤーを指定します。

| 値 | 説明 |
|-----------|------|
| `0`       | 現在選択中のレイヤーへドロップ |
| `正の整数` | そのレイヤー番号へドロップ |
| `負の整数` | 現在の表示位置からの相対位置へドロップ。<br>タイムラインが Layer 3 から表示されているなら `-2` で Layer 4 へドロップ |

#### frameAdvance フィールド

`frameAdvance` フィールドは、ファイルドロップ後にタイムラインカーソルを何フレーム進めるかを指定します。

#### margin フィールド

`margin` フィールドは、挿入先に既にオブジェクトが存在する場合の処理方法を指定します。

| 値       | 説明 |
|----------|------|
| `-1`     | 挿入を諦めます |
| `0` 以上 | 既存のオブジェクトの後ろに指定したフレーム数分の隙間を空けて挿入します |

#### files フィールド

`files` フィールドは、ドロップするファイルのフルパス（UTF-8）の配列を指定します。

## 実装例

C 言語による実装例です。

```c
#include <stdint.h>
#include <stdio.h>

#define UNICODE
#include <Windows.h>

struct GCMZDropsData {
  uint32_t Window;
  int32_t Width;
  int32_t Height;
  int32_t VideoRate;
  int32_t VideoScale;
  int32_t AudioRate;
  int32_t AudioCh;
  int32_t GCMZAPIVer;
  wchar_t ProjectPath[MAX_PATH];
  uint32_t Flags;
  uint32_t AviUtlVer;
  uint32_t GCMZVer;
};

int main(int argc, char *argv[]) {
  HANDLE hMutex = NULL;
  HANDLE hFMO = NULL;
  struct GCMZDropsData *p = NULL;
  BOOL mutexLocked = FALSE;

  //  Mutex を開く
  hMutex = OpenMutex(MUTEX_ALL_ACCESS, FALSE, TEXT("GCMZDropsMutex"));
  if (hMutex == NULL) {
    printf("OpenMutex failed.\n");
    goto Cleanup;
  }

  // ファイルマッピングオブジェクトを開く
  hFMO = OpenFileMapping(FILE_MAP_READ, FALSE, TEXT("GCMZDrops"));
  if (hFMO == NULL) {
    printf("OpenFileMapping failed.\n");
    goto Cleanup;
  }

  // 共有メモリをマップ
  p = MapViewOfFile(hFMO, FILE_MAP_READ, 0, 0, 0);
  if (p == NULL) {
    printf("MapViewOfFile failed.\n");
    goto Cleanup;
  }

  //  Mutex を取得
  if (WaitForSingleObject(hMutex, INFINITE) != WAIT_OBJECT_0) {
    printf("WaitForSingleObject failed.\n");
    goto Cleanup;
  }
  mutexLocked = TRUE;

  // API ウィンドウの確認
  if (!p->Window) {
    printf("The target window is NULL.\n");
    goto Cleanup;
  }

  // プロジェクトが開かれているか確認
  if (!p->Width) {
    printf("The project is not open.\n");
    goto Cleanup;
  }

  // APIバージョン情報を元に AviUtl ExEdit2 かどうかをチェック
  printf("GCMZAPIVer: %d\n", p->GCMZAPIVer);
  if (p->GCMZAPIVer != 3) {
    printf("AviUtl and GCMZDrops too old, please update.\n");
    goto Cleanup;
  }

  // プロジェクト情報の表示
  printf("ProjectPath: %ls\n", p->ProjectPath);
  printf("Width: %d, Height: %d\n", p->Width, p->Height);
  printf("VideoRate: %d, VideoScale: %d\n", p->VideoRate, p->VideoScale);
  printf("AudioRate: %d, AudioCh: %d\n", p->AudioRate, p->AudioCh);

  if (p->GCMZAPIVer >= 3) {
    printf("AviUtlVer: %d\n", p->AviUtlVer);
    printf("GCMZVer: %d\n", p->GCMZVer);
  }

  // ファイルドロップリクエストを送信（JSON フォーマット）
  {
    // JSON データを構築
    char json[] = "{"
      "\"layer\":0,"       // 0 = 選択中のレイヤー
      "\"frameAdvance\":0,"
      "\"margin\":10,"      // オブジェクトがある場合、後ろに10フレーム空けて挿入
      "\"files\":[\"C:\\\\test\\\\image.png\"]"
    "}";
    SendMessage(
      (HWND)(intptr_t)p->Window,
      WM_COPYDATA,
      (WPARAM)GetConsoleWindow(),
      (LPARAM)&(COPYDATASTRUCT){
        .dwData = 2,
        .cbData = (DWORD)strlen(json),
        .lpData = json
      }
    );
  }

Cleanup:
  if (p) {
    UnmapViewOfFile(p);
  }
  if (mutexLocked) {
    ReleaseMutex(hMutex);
  }
  if (hFMO) {
    CloseHandle(hFMO);
  }
  if (hMutex) {
    CloseHandle(hMutex);
  }
  return 0;
}
```

## 変更履歴

### API バージョン 3（GCMZDrops v2.0）

- AviUtl ExEdit2 対応
- `AviUtlVer` および `GCMZVer` フィールドを追加
- 選択中のレイヤーをドロップ先として指定可能にした
- `margin` パラメーターを追加

### API バージョン 2（GCMZDrops v0.3.23）

- `Flags` フィールドを追加（翻訳パッチ検出用）

### API バージョン 1（GCMZDrops v0.3.12）

- `GCMZAPIVer` フィールドを追加
- `ProjectPath` フィールドを追加
- `dwData = 1` で JSON フォーマットをサポート

### API バージョン 0（GCMZDrops v0.3）

- AviUtl1 に外部のアプリケーションからファイルをドロップするための API を初実装
