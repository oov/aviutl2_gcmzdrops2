# GCMZDrops ハンドラースクリプトリファレンス

このドキュメントでは、GCMZDrops のハンドラースクリプトを作成するための情報を提供します。

## 目次

### ハンドラースクリプト

- [概要](#概要)
- [基本構造](#基本構造)
- [フック関数](#フック関数)
  - [drag\_enter](#drag_enter)
  - [drag\_leave](#drag_leave)
  - [drop](#drop)
- [データ構造](#データ構造)
  - [files テーブル](#files-テーブル)
  - [state テーブル](#state-テーブル)

### グローバル関数

- [debug\_print](#debug_print)
- [i18n](#i18n)

### gcmz ネームスペース

- [gcmz.get\_project\_data](#gcmzget_project_data)
- [gcmz.get\_versions](#gcmzget_versions)
- [gcmz.create\_temp\_file](#gcmzcreate_temp_file)
- [gcmz.save\_file](#gcmzsave_file)
- [gcmz.convert\_encoding](#gcmzconvert_encoding)
- [gcmz.decode\_exo\_text](#gcmzdecode_exo_text)

---

# ハンドラースクリプト

## 概要

ハンドラースクリプトは、AviUtl ExEdit2 のタイムラインにファイルをドロップしたときの処理をカスタマイズするための Lua スクリプトです。

ハンドラースクリプトは GCMZDrops のスクリプトディレクトリに `.lua` ファイルとして配置することで AviUtl ExEdit2 の起動時に読み込まれます。

各スクリプトはドラッグ＆ドロップ操作のライフサイクルに対応するフック関数を実装できます。

## 基本構造

ハンドラースクリプトはテーブルを返す必要があります。  
このテーブルには `priority` フィールドとフック関数を含めることができます。

```lua
local M = {}

-- 優先度（省略可能、デフォルト: 1000）
-- 数値が小さいほど先に実行されます
M.priority = 1000

function M.drag_enter(files, state)
  -- ドラッグ開始時の処理
  return true
end

function M.drag_leave()
  -- ドラッグがタイムラインから離れたときの処理
end

function M.drop(files, state)
  -- ドロップ時の処理
  return true
end

return M
```

### 優先度

`priority` フィールドは、複数のハンドラースクリプトがある場合の実行順序を決定します。

| 値 | 説明 |
|---|---|
| 小さい値（例: 100） | 先に実行される（高優先度） |
| 1000 | デフォルト値 |
| 大きい値（例: 2000） | 後に実行される（低優先度） |

## フック関数

### drag_enter

ファイルがタイムラインにドラッグされたときに呼び出されます。

#### 構文

```lua
function M.drag_enter(files, state)
  -- 処理
  return true  -- または false
end
```

#### パラメーター

| パラメーター | 型 | 説明 |
|-----------|------|-------------|
| `files` | table | ドラッグされたファイルのリスト（[files テーブル](#files-テーブル)を参照） |
| `state` | table | キー状態（[state テーブル](#state-テーブル)を参照） |

#### 戻り値

| 戻り値 | 説明 |
|-------|------|
| `true` | このハンドラーがファイルを受け入れ、以降のフック関数（`drag_leave`、`drop`）も呼び出されます |
| `false` | このハンドラーがファイルを拒否し、以降のフック関数は呼び出されません |

#### 注意事項

- `drag_enter` 関数内では、**ドロップされたファイルの読み取りや変更は行わないでください**。
- `state` に含まれるキー状態を利用した判定はここでは行わず、`drop` のタイミングでチェックしてください。

#### 使用例

`drag_enter` では主にファイルの拡張子をチェックして、このハンドラーで処理すべきファイルかどうかを判断します。  
この段階では、ファイルの拡張子などのメタ情報のみを参照して、ハンドラーがファイルを処理すべきかを判断してください。  
ここで重い処理を行うとドラッグ操作のレスポンスに影響を及ぼすため注意が必要です。

```lua
function M.drag_enter(files, state)
  -- PNG ファイルがあるかチェック
  for _, file in ipairs(files) do
    if file.filepath:sub(-4):lower() == ".png" then
      return true
    end
  end
  return false
end
```

---

### drag_leave

ドラッグされたファイルがタイムラインから離れたときに呼び出されます。

#### 注意事項

- この関数は `drag_enter` で `true` を返したハンドラーでのみ呼び出されます。

#### 構文

```lua
function M.drag_leave()
  -- クリーンアップ処理
end
```

#### パラメーター

なし。

#### 戻り値

戻り値は使用されません。

---

### drop

ファイルがタイムラインにドロップされたときに呼び出されます。

#### 注意事項

- この関数は `drag_enter` で `true` を返したハンドラーでのみ呼び出されます。

#### 構文

```lua
function M.drop(files, state)
  -- ドロップ処理
  return true
end
```

#### パラメーター

| パラメーター | 型 | 説明 |
|-----------|------|-------------|
| `files` | table | ドロップされたファイルのリスト（[files テーブル](#files-テーブル)を参照） |
| `state` | table | キー状態（[state テーブル](#state-テーブル)を参照） |

#### 戻り値

戻り値は現在使用されていません。

#### ファイルリストの変更

`drop` 関数内で `files` テーブルを直接変更することで、最終的に AviUtl2 に渡されるファイルリストを変更できます。  
これを利用して、ファイルの変換や追加、除外などが可能です。

```lua
function M.drop(files, state)
  -- 一時ファイルを作成して追加
  local temp_path = gcmz.create_temp_file("converted.txt")
  -- ファイルに何か処理を行う
  table.insert(files, {
    filepath = temp_path,
    mimetype = "text/plain",
    temporary = true  -- 一時ファイルとしてマーク
    memo = "This is a converted file." -- 他のスクリプトが参照できるように情報を追加することもできます
  })
  return true
end
```

---

## データ構造

### files テーブル

`files` テーブルは、ドラッグ/ドロップされたファイルの配列です。

```lua
{
  {
    filepath = "C:\\Path\\To\\File1.png",
    mimetype = "image/png",
    temporary = false
  },
  {
    filepath = "C:\\Path\\To\\File2.wav",
    mimetype = "audio/wav",
    temporary = false
  },
  -- ...
}
```

#### フィールド

| フィールド | 型 | 説明 |
|-------|------|-------------|
| `filepath` | string | ファイルのフルパス（UTF-8） |
| `mimetype` | string | ファイルの MIME タイプ（例: `"image/png"`、`"audio/wav"`）。不明な場合は空文字列 |
| `temporary` | boolean | 一時ファイルかどうか。`true` の場合、ドロップ処理完了後に自動的に削除される対象となります |

#### ファイルリストの変更

ハンドラースクリプト内で `files` テーブルを直接変更できます。

```lua
-- ファイルの追加
table.insert(files, {
  filepath = "C:\\Path\\To\\NewFile.txt",
  mimetype = "text/plain",
  temporary = false
})

-- ファイルの削除
table.remove(files, 1)

-- ファイルの置換
files[1] = {
  filepath = "C:\\Path\\To\\Replaced.png",
  mimetype = "image/png",
  temporary = true
}
```

---

### state テーブル

`state` テーブルは、キー/マウスボタンの状態を含みます。

```lua
{
  control = false,          -- Ctrl キーが押されているか
  shift = true,             -- Shift キーが押されているか
  alt = false,              -- Alt キーが押されているか
  win = false,              -- Windows キーが押されているか
  lbutton = true,           -- 左マウスボタンが押されているか
  mbutton = false,          -- 中マウスボタンが押されているか
  rbutton = false,          -- 右マウスボタンが押されているか
  from_external_api = false -- 外部 API 経由の投げ込みか
}
```

#### フィールド

| フィールド | 型 | 説明 |
|-------|------|-------------|
| `control` | boolean | Ctrl キーが押されている場合 `true` |
| `shift` | boolean | Shift キーが押されている場合 `true` |
| `alt` | boolean | Alt キーが押されている場合 `true` |
| `win` | boolean | Windows キー（左右どちらか）が押されている場合 `true` |
| `lbutton` | boolean | 左マウスボタンが押されている場合 `true` |
| `mbutton` | boolean | 中マウスボタンが押されている場合 `true` |
| `rbutton` | boolean | 右マウスボタンが押されている場合 `true` |
| `from_external_api` | boolean | 外部連携 API 経由での投げ込みの場合 `true`、通常のドラッグ＆ドロップの場合 `false` |

---

## 完全な例

### 特定のファイルタイプを処理するハンドラー

```lua
-- PNG ファイルを処理するハンドラー
local M = {}

M.priority = 500  -- 高めの優先度

function M.drag_enter(files, state)
  -- PNG ファイルがあるかチェック（拡張子ベース）
  for _, file in ipairs(files) do
    if file.filepath:sub(-4):lower() == ".png" then
      return true
    end
  end
  return false
end

function M.drag_leave()
  -- 特に何もしない
end

function M.drop(files, state)
  -- Shift キーが押されている場合のみ特別な処理
  if state.shift then
    debug_print("Shift + Drop detected for PNG files")
    -- ここで何か特別な処理を行う
  end
end

return M
```

### テキストを置換するハンドラー

```lua
-- テキストファイル内の特定のワードを置換するハンドラー
-- 例: 「マリリンマンソン」を「マソソソマソソソ」に置換
local M = {}

M.priority = 800

function M.drag_enter(files, state)
  for _, file in ipairs(files) do
    if file.mimetype == "text/plain" then
      return true
    end
  end
  return false
end

function M.drop(files, state)
  for i, file in ipairs(files) do
    if file.mimetype == "text/plain" then
      -- ファイルを読み込む
      local f = io.open(file.filepath, "rb")
      if f then
        local content = f:read("*a")
        f:close()
        
        local replaced = content:gsub("マリリンマンソン", "マソソソマソソソ")
        
        -- 内容が変更された場合のみ一時ファイルを作成
        if replaced ~= content then
          local temp_path = gcmz.create_temp_file("replaced.txt")
          local out = io.open(temp_path, "wb")
          if out then
            out:write(replaced)
            out:close()
            
            -- ファイルリストを更新
            files[i] = {
              filepath = temp_path,
              mimetype = "text/plain",
              temporary = true
            }
          end
        end
      end
    end
  end
end

return M
```

---

# グローバル関数

以下の関数はグローバルスコープから直接呼び出すことができます。

---

## debug_print

デバッグメッセージをログに出力します。

### 構文

```lua
debug_print(message)
```

### パラメーター

| パラメーター | 型 | 説明 |
|-----------|------|-------------|
| `message` | string | 出力するメッセージ（UTF-8） |

### 戻り値

なし。

### 説明

デバッグ情報をログシステムに出力します。この関数は開発やトラブルシューティングの際に便利です。

### 例

```lua
debug_print("Processing started...")
debug_print("File path: " .. filepath)
```

---

## i18n

言語コードをキーとしたテーブルから、現在のシステム言語に最も適したテキストを選択して返します。

### 構文

```lua
local text = i18n(text_table)
local text = i18n(text_table, preferred_lang)
```

### パラメーター

| パラメーター | 型 | 説明 |
|-----------|------|-------------|
| `text_table` | table | 言語コードをキー、テキストを値とするテーブル |
| `preferred_lang` | string (省略可) | 優先する言語コード（例: `"ja_JP"`）。指定した場合、システム設定より優先されます |

### 戻り値

選択されたテキスト文字列を返します。適切なテキストが見つからない場合は `nil` を返します。

### 言語選択の優先順位

1. **オーバーライド言語**: 第二引数で指定された言語（指定時のみ）
2. **完全一致**: システムの優先言語リストと完全に一致するキー
3. **プライマリ言語一致**: 言語コードの前半部分（例: `ja` in `ja_JP`）が一致するキー
4. **en\_US フォールバック**: 上記で見つからない場合、`en_US` キー
5. **最初のキー**: それでも見つからない場合、テーブル内の最初の有効なキー

### 対応言語

現在、以下の言語がサポートされています：

- `ja_JP` - 日本語
- `zh_CN` - 簡体字中国語
- `en_US` - 英語（デフォルト）

### 例

```lua
-- 基本的な使用法（システム言語に従う）
debug_print(i18n({
  ja_JP = [=[こんにちは]=],
  en_US = [=[Hello]=],
  zh_CN = [=[你好]=],
}))

-- string.format と組み合わせた使用例
debug_print(string.format(i18n({
    ja_JP = [=[%s: 処理を続行します。]=],
    en_US = [=[%s: Continue processing.]=],
    zh_CN = [=[%s: 继续处理]=],
  }),
  filename
))
```

---

## gcmz ネームスペース

以下の関数は `gcmz` グローバルテーブルを通じてアクセスします。

---

## gcmz.get_project_data

現在の AviUtl ExEdit2 プロジェクト情報を取得します。

### 構文

```lua
local data = gcmz.get_project_data()
```

### パラメーター

なし。

### 戻り値

以下のフィールドを含むテーブルを返します：

| フィールド | 型 | 説明 |
|-------|------|-------------|
| `width` | integer | 動画の幅（ピクセル単位） |
| `height` | integer | 動画の高さ（ピクセル単位） |
| `video_rate` | integer | 動画フレームレートの分子 |
| `video_scale` | integer | 動画フレームレートの分母 |
| `sample_rate` | integer | 音声サンプリングレート（Hz） |
| `project_path` | string または nil | プロジェクトファイルのパス（UTF-8）、未保存の場合は nil |

> **注意**: 実際のフレームレート（fps）は `video_rate / video_scale` で計算できます。

### エラー

- プロジェクトデータプロバイダーが設定されていない場合、エラーをスローします。

### 例

```lua
local project = gcmz.get_project_data()
print("解像度: " .. project.width .. "x" .. project.height)
print("フレームレート: " .. (project.video_rate / project.video_scale) .. " fps")
print("サンプリングレート: " .. project.sample_rate .. " Hz")
if project.project_path then
    print("プロジェクトパス: " .. project.project_path)
end
```

---

## gcmz.get_versions

AviUtl ExEdit2 と GCMZDrops のバージョン情報を取得します。

### 構文

```lua
local versions = gcmz.get_versions()
```

### パラメーター

なし。

### 戻り値

以下のフィールドを含むテーブルを返します：

| フィールド | 型 | 説明 |
|-------|------|-------------|
| `aviutl2_ver` | integer | AviUtl ExEdit2 のバージョン番号 |
| `gcmz_ver` | integer | GCMZDrops のバージョン番号 |

### 例

```lua
local ver = gcmz.get_versions()
print("AviUtl ExEdit2 バージョン: " .. ver.aviutl2_ver)
print("GCMZDrops バージョン: " .. ver.gcmz_ver)
```

---

## gcmz.create_temp_file

指定されたファイル名に近い名前を持つ一時ファイルを作成します。

### 構文

```lua
local temp_path = gcmz.create_temp_file(filename)
```

### パラメーター

| パラメーター | 型 | 説明 |
|-----------|------|-------------|
| `filename` | string | 一時ファイルのベースファイル名（UTF-8） |

### 戻り値

一時ファイルへのフルパスを返します。

### エラー

- `filename` が指定されていない場合、エラーをスローします。

### 例

```lua
local temp_path = gcmz.create_temp_file("output.txt")
print("一時ファイルの保存先: " .. temp_path)
```

---

## gcmz.save_file

ソースファイルを指定されたファイル名で管理された保存ディレクトリにコピーします。

### 構文

```lua
local dest_path = gcmz.save_file(src_path, dest_filename)
```

### パラメーター

| パラメーター | 型 | 説明 |
|-----------|------|-------------|
| `src_path` | string | ソースファイルのパス（UTF-8） |
| `dest_filename` | string | 保存先のファイル名（フルパスではない）（UTF-8） |

### 戻り値

ファイルが保存された保存先のフルパスを返します（UTF-8 文字列）。

### エラー

- `src_path` または `dest_filename` が指定されていない場合、エラーをスローします。
- ソースファイルの読み取りまたは保存先への書き込みができない場合、エラーをスローします。

### 例

```lua
local saved_path = gcmz.save_file("C:/temp/image.png", "saved_image.png")
print("ファイルの保存先: " .. saved_path)
```

---

## gcmz.convert_encoding

テキストをある文字エンコーディングから別のエンコーディングに変換します。

### 構文

```lua
local converted = gcmz.convert_encoding(text, src_encoding, dest_encoding)
```

### パラメーター

| パラメーター | 型 | 説明 |
|-----------|------|-------------|
| `text` | string | 変換するテキスト |
| `src_encoding` | string | 変換元のエンコーディング名 |
| `dest_encoding` | string | 変換先のエンコーディング名 |

### 対応エンコーディング

| 名前 | エイリアス | 説明 |
|------|---------|-------------|
| `sjis` | `shift_jis` | Shift_JIS（Windows コードページ 932） |
| `utf8` | `utf-8` | UTF-8 |
| `utf16le` | `utf-16le` | UTF-16 リトルエンディアン |
| `utf16be` | `utf-16be` | UTF-16 ビッグエンディアン |
| `eucjp` | `euc-jp` | EUC-JP |
| `iso2022jp` | `iso-2022-jp` | ISO-2022-JP |
| `ansi` | - | システム ANSI コードページ |

### 戻り値

変換されたテキストを文字列として返します。

### エラー

- 必須パラメーターが指定されていない場合、エラーをスローします。
- エンコーディング名がサポートされていない場合、エラーをスローします。
- 変換に失敗した場合、エラーをスローします。

### 例

```lua
-- UTF-8 から Shift_JIS に変換
local sjis_text = gcmz.convert_encoding("こんにちは", "utf8", "sjis")

-- Shift_JIS から UTF-8 に変換
local utf8_text = gcmz.convert_encoding(sjis_text, "sjis", "utf8")
```

---

## gcmz.decode_exo_text

EXO テキストフィールド形式（16進数エンコードされた UTF-16LE）を UTF-8 文字列にデコードします。

### 構文

```lua
local text = gcmz.decode_exo_text(hex_string)
```

### パラメーター

| パラメーター | 型 | 説明 |
|-----------|------|-------------|
| `hex_string` | string | EXO ファイルからの16進数エンコードされた UTF-16LE 文字列 |

### EXO テキスト形式

EXO ファイルはテキストフィールドを16進数エンコードされた UTF-16LE 文字列として保存します。各 UTF-16LE コードユニットはリトルエンディアンのバイト順で4桁の16進数字として表現されます。

例：
- `"41004200"` は `"AB"` にデコードされます（0x0041 = 'A', 0x0042 = 'B'）
- 文字列はヌルコードユニット（`"0000"`）で終端します

### 戻り値

デコードされたテキストを UTF-8 文字列として返します。

### エラー

- `hex_string` が指定されていない場合、エラーをスローします。
- 16進数文字列の長さが4の倍数でない場合、エラーをスローします。
- 文字列に無効な16進数文字が含まれている場合、エラーをスローします。

### 例

```lua
-- EXO 形式から "Hello" をデコード
local text = gcmz.decode_exo_text("480065006C006C006F00")
print(text)  -- 出力: Hello

-- 空文字列の処理
local empty = gcmz.decode_exo_text("")
print(empty)  -- 出力: （空文字列）
```

---

## エラーハンドリング

すべての関数は失敗時に Lua エラーをスローする可能性があります。エラーハンドリングには `pcall` または `xpcall` を使用してください：

```lua
local ok, result = pcall(function()
    return gcmz.convert_encoding("hello", "utf8", "invalid_encoding")
end)

if ok then
    print("変換結果: " .. result)
else
    print("エラー: " .. result)
end
```
