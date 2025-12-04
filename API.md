# GCMZDrops 外部連携 API リファレンス

このドキュメントでは、GCMZDrops の外部連携 API を使用して、別のアプリケーションから AviUtl ExEdit2 のタイムラインへファイルをドロップする方法を説明します。

## 目次

- [概要](#概要)
- [API バージョン](#api-バージョン)
- [共有メモリ構造](#共有メモリ構造)
  - [GCMZDropsData 構造体](#gcmzdropsdata-構造体)
  - [GCMZAPIVer フィールド](#gcmzapiver-フィールド)
  - [Flags フィールド](#flags-フィールド)
- [ファイルドロップ API](#ファイルドロップ-api)
  - [JSON フォーマット（推奨）](#json-フォーマット推奨)
  - [レガシーフォーマット（非推奨）](#レガシーフォーマット非推奨)
- [使用例](#使用例)
  - [C 言語による実装例](#c-言語による実装例)
- [注意事項](#注意事項)

---

## 概要

GCMZDrops 外部連携 API は、以下の仕組みで動作します：

1. **共有メモリ**: プロジェクト情報の取得に使用
2. **ミューテックス**: 排他制御に使用
3. **WM_COPYDATA**: ファイルドロップリクエストの送信に使用

外部アプリケーションは共有メモリからプロジェクト情報を取得し、`WM_COPYDATA` メッセージを送信することでファイルをタイムラインにドロップできます。

---

## 共有メモリ構造

### 名前付きオブジェクト

| オブジェクト | 名前 | 用途 |
|-------------|------|------|
| ミューテックス | `GCMZDropsMutex` | 排他制御 |
| ファイルマッピング | `GCMZDrops` | プロジェクト情報の共有 |

### GCMZDropsData 構造体

```c
struct GCMZDropsData {
  uint32_t Window;                   // API ウィンドウハンドル
  int32_t Width;                     // 動画の幅（ピクセル）
  int32_t Height;                    // 動画の高さ（ピクセル）
  int32_t VideoRate;                 // フレームレート分子
  int32_t VideoScale;                // フレームレート分母
  int32_t AudioRate;                 // 音声サンプリングレート（Hz）
  int32_t AudioCh;                   // 音声チャンネル数
  int32_t GCMZAPIVer;                // API バージョン（v0.3.12 以降）
  wchar_t ProjectPath[MAX_PATH];     // プロジェクトファイルパス（v0.3.12 以降）
  uint32_t Flags;                    // フラグ（v0.3.23 以降、API バージョン 2 以上）
  uint32_t AviUtl2Ver;               // AviUtl ExEdit2 バージョン（API バージョン 3 以上）
  uint32_t GCMZVer;                  // GCMZDrops バージョン（API バージョン 3 以上）
};
```

#### フィールド詳細

| フィールド | 説明 |
|-----------|------|
| `Window` | ファイルドロップリクエストを送信する先のウィンドウハンドル。`0` の場合は API が無効 |
| `Width` | 動画の幅（ピクセル）。`0` の場合はプロジェクトが開かれていない |
| `Height` | 動画の高さ（ピクセル） |
| `VideoRate` | フレームレートの分子。実際の fps は `VideoRate / VideoScale` |
| `VideoScale` | フレームレートの分母 |
| `AudioRate` | 音声サンプリングレート（Hz） |
| `AudioCh` | 音声チャンネル数 |
| `GCMZAPIVer` | API バージョン番号 |
| `ProjectPath` | プロジェクトファイルのフルパス。未保存の場合は空文字列 |
| `Flags` | 各種フラグ（後述） |
| `AviUtl2Ver` | AviUtl ExEdit2 のバージョン番号 |
| `GCMZVer` | GCMZDrops のバージョン番号 |

### GCMZAPIVer フィールド

`GCMZAPIVer` フィールドは API のバージョン番号を示します。

| バージョン | 対応環境 | 備考 |
|----------:|---------|------|
| 0 | AviUtl 1.00/1.10 + ExEdit 0.92 + GCMZDrops v0.3 〜 v0.3.11 | `GCMZAPIVer` フィールドが存在しないため検出不可、非推奨 |
| 1 | AviUtl 1.00/1.10 + ExEdit 0.92 + GCMZDrops v0.3.12 〜 v0.3.22 | `GCMZAPIVer` と `ProjectPath` フィールドを追加 |
| 2 | AviUtl 1.00/1.10 + ExEdit 0.92 + GCMZDrops v0.3.23 以降 | `Flags` フィールドを追加（翻訳パッチ検出用） |
| 3 | AviUtl ExEdit2 + GCMZDrops v2.0 以降 | `AviUtl2Ver` と `GCMZVer` フィールドを追加、`Layer = 0` を選択中のレイヤーとして扱うように変更、`*.exo` の簡易的な自動変換を実装 |

### Flags フィールド

`Flags` フィールドは API バージョン 2 以上で利用可能です。

| ビット | 説明 |
|-------|------|
| 0x00000001 | 英語翻訳パッチが適用されている（AviUtl1 用） |
| 0x00000002 | 簡体字中国語翻訳パッチが適用されている（AviUtl1 用） |

---

## ファイルドロップ API

ファイルをドロップするには、`WM_COPYDATA` メッセージを使用します。

```c
SendMessage(window, WM_COPYDATA, (WPARAM)sender_window, (LPARAM)&cds);
```

### COPYDATASTRUCT の構成

| フィールド | 説明 |
|-----------|------|
| `dwData` | データフォーマット識別子（0, 1, または 2） |
| `cbData` | データサイズ（バイト） |
| `lpData` | データへのポインタ |

#### dwData の値

| 値 | フォーマット | 備考 |
|---|-------------|------|
| 0 | レガシー（wchar_t） | 非推奨。後方互換性のためのみ |
| 1 | JSON（UTF-8） | AviUtl ExEdit2 では `*.exo` を自動変換を試みる |
| 2 | JSON（UTF-8） | `*.exo` 自動変換なし、`layer = 0` で選択中のレイヤー指定が可能 |

AviUtl ExEdit2 では `*.exo` ファイルがサポートされていません。  
`dwData = 0` か `dwData = 1` を使用すると GCMZDrops が `*.exo` ファイルを新形式に自動変換しますが、これは簡易的なもので多くの場合に対応できません。  
最初から `*.object` 形式を使用してください。

### JSON フォーマット（推奨）

`dwData = 1` または `dwData = 2` で使用する JSON フォーマットです。

```json
{
  "layer": 1,
  "frameAdvance": 100,
  "files": [
    "C:\\Path\\To\\File1.png",
    "C:\\Path\\To\\File2.wav"
  ]
}
```

#### フィールド

| フィールド | 型 | 必須 | 説明 |
|-----------|---|-----|------|
| `layer` | integer | いいえ | ドロップ先のレイヤー番号。マイナスのときは現在の表示位置からの想定指定、プラスのときはそのレイヤー番号、`0` の場合は選択中のレイヤー(dwData = 2 のみ) |
| `frameAdvance` | integer | いいえ | カーソル位置からのフレームオフセット。省略時は `0`（現在のカーソル位置） |
| `files` | array | はい | ドロップするファイルのフルパス（UTF-8）の配列。空配列は不可 |

---

## 使用例

### C 言語による実装例

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
  uint32_t AviUtl2Ver;
  uint32_t GCMZVer;
};

int main(int argc, char *argv[]) {
  HANDLE hMutex = NULL;
  HANDLE hFMO = NULL;
  struct GCMZDropsData *p = NULL;
  BOOL mutexLocked = FALSE;

  // ミューテックスを開く
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

  // ミューテックスを取得
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

  // バージョン確認
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
    printf("AviUtl2Ver: %d\n", p->AviUtl2Ver);
    printf("GCMZVer: %d\n", p->GCMZVer);
  }

  // ファイルドロップリクエストを送信（JSON フォーマット）
  {
    // JSON データを構築
    char json[] = "{"
      "\"layer\":0,"       // 0 = 選択中のレイヤー
      "\"frameAdvance\":0,"
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

---

## 注意事項

### 一般的な注意事項

- **タイムラインウィンドウの表示**  
  タイムラインウィンドウが表示されていないと、アイテムの挿入位置を判定できずに API の実行に失敗することがあります。

- **挿入位置について**  
  挿入先に既にオブジェクトがある場合など、十分なスペースがない場合は想定した場所に挿入されません。

- **複数ファイルのドロップについて**  
  十分なスペースがない場合は一部のファイルだけがずれた位置に配置されることがあります。

- **多重起動について**  
  API が使えるのは最初に起動したインスタンスのみです。AviUtl を多重起動しても、2 つ目以降では API は無効状態になります。

### セキュリティ

- ファイルパスには `..` を含むことはできません
- ファイルパスは絶対パス（ドライブレター付き）である必要があります
- ファイルパスの最大長は 1024 文字です

---

## 変更履歴

### API バージョン 3（GCMZDrops v2.0）

- AviUtl ExEdit2 対応
- `aviutl2_ver` および `gcmz_ver` フィールドを追加
- `*.exo` ファイルの自動変換機能を追加（`dwData = 0` または `dwData = 1` の使用時のみ）
- `dwData = 2` のときに `layer = 0` で選択中のレイヤーを指定可能に変更

### API バージョン 2（GCMZDrops v0.3.23）

- `flags` フィールドを追加（翻訳パッチ検出用）

### API バージョン 1（GCMZDrops v0.3.12）

- `gcmz_api_ver` フィールドを追加
- `project_path` フィールドを追加
- JSON フォーマットをサポート（`dwData = 1`）
