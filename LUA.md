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

- [gcmz.get\_media\_info](#gcmzget_media_info)
- [gcmz.get\_project\_data](#gcmzget_project_data)
- [gcmz.get\_script\_directory](#gcmzget_script_directory)
- [gcmz.get\_versions](#gcmzget_versions)
- [gcmz.create\_temp\_file](#gcmzcreate_temp_file)
- [gcmz.save\_file](#gcmzsave_file)
- [gcmz.convert\_encoding](#gcmzconvert_encoding)
- [gcmz.decode\_exo\_text](#gcmzdecode_exo_text)

### ini モジュール

- [概要](#ini-概要)
- [ini.new](#ininew)
- [ini.load](#iniload)
- [ini:save](#inisave)
- [ini:get](#iniget)
- [ini:set](#iniset)
- [ini:delete](#inidelete)
- [ini:deletesection](#inideletesection)
- [ini:sections](#inisections)
- [ini:keys](#inikeys)
- [ini:sectionexists](#inisectionexists)
- [ini:exists](#iniexists)
- [文字列への変換](#文字列への変換)

### json モジュール

- [概要](#json-概要)
- [json.decode](#jsondecode)
- [json.encode](#jsonencode)

---

# ハンドラースクリプト

## 概要

ハンドラースクリプトは、AviUtl ExEdit2 のタイムラインにファイルをドロップしたときの処理をカスタマイズするための Lua スクリプトです。

ハンドラースクリプトは GCMZDrops のスクリプトディレクトリに `.lua` ファイルとして配置することで AviUtl ExEdit2 の起動時に読み込まれます。

各スクリプトはドラッグ＆ドロップ操作のライフサイクルに対応するフック関数を実装できます。

## 基本構造

ハンドラースクリプトはテーブルを返す必要があります。  
このテーブルには `name` フィールド（必須）と `priority` フィールド（省略可）、およびフック関数を含めることができます。

```lua
local M = {}

-- ハンドラー名（必須）
M.name = "My Handler"

-- 優先度（省略時は 1000）
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

### ハンドラー名

`name` はハンドラーの識別名を指定します。  
この値は必須です。`name` フィールドがないテーブルはハンドラーとして登録されません。

設定ダイアログの「ハンドラー」タブでハンドラー一覧を確認する際などに使用されます。

### 優先度

`priority` は、複数のハンドラースクリプトがある場合の実行順序を決定します。  
省略した場合は 1000 が使用されます。

| 値 | 説明 |
|---|---|
| 小さい値（例: 100） | 先に実行される（高優先度） |
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
| `filepath` | string | ファイルのフルパス |
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

M.name = i18n({
  ja_JP = [=[PNGファイルハンドラーテスト]=],
  en_US = [=[PNG File Handler Test]=],
  zh_CN = [=[PNG文件处理程序测试]=],
})

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

M.name = i18n({
  ja_JP = [=[マリリンマンソン置換ハンドラー]=],
  en_US = [=[Marilyn Manson Replacement Handler]=],
  zh_CN = [=[玛丽莲·曼森替换处理程序]=],
})

M.priority = 1000

function M.drag_enter(files, state)
  for _, file in ipairs(files) do
    if file.filepath:sub(-4):lower() == ".txt" then
      return true
    end
  end
  return false
end

function M.drop(files, state)
  for i, file in ipairs(files) do
    if file.filepath:sub(-4):lower() == ".txt" then
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
              temporary = true,
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
| `message` | string | 出力するメッセージ |

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
    ja_JP = [=[%s: 処理を続行します]=],
    en_US = [=[%s: Continue processing]=],
    zh_CN = [=[%s: 继续处理]=],
  }),
  filename
))
```

---

## gcmz ネームスペース

以下の関数は `gcmz` グローバルテーブルを通じてアクセスします。

---

## gcmz.get_media_info

指定されたメディアファイルの情報を取得します。

### 構文

```lua
local info = gcmz.get_media_info(filepath)
```

### パラメーター

| パラメーター | 型 | 説明 |
|-----------|------|-------------|
| `filepath` | string | メディアファイルのパス |

### 戻り値

成功時は以下のフィールドを含むテーブルを返します。失敗時は `nil, errmsg` を返します。

| フィールド | 型 | 説明 |
|-------|------|-------------|
| `video_track_num` | integer | 動画トラック数（動画がない場合は 0） |
| `audio_track_num` | integer | 音声トラック数（音声がない場合は 0） |
| `total_time` | number または nil | 総再生時間（秒）。静止画の場合は `nil` |
| `width` | integer または nil | 動画の幅（ピクセル単位）。音声ファイルの場合は `nil` |
| `height` | integer または nil | 動画の高さ（ピクセル単位）。音声ファイルの場合は `nil` |

### エラー

- `filepath` が指定されていない場合、エラーをスローします。
- ファイルが見つからない場合やメディア情報を取得できない場合は `nil, errmsg` を返します。

### 例

```lua
local info, err = gcmz.get_media_info("C:/Videos/sample.mp4")
if not info then
    debug_print("メディア情報の取得に失敗: " .. err)
    return
end
print("動画トラック数: " .. info.video_track_num)
print("音声トラック数: " .. info.audio_track_num)

-- 動画かどうかを判定
if info.video_track_num > 0 then
    print("解像度: " .. info.width .. "x" .. info.height)
end

-- 静止画かどうかを判定
if info.video_track_num > 0 and info.total_time == nil then
    print("これは静止画ファイルです")
end

-- 動画（静止画以外）かどうかを判定
if info.video_track_num > 0 and info.total_time then
    print("総再生時間: " .. info.total_time .. " 秒")
end

-- 音声のみかどうかを判定
if info.video_track_num == 0 and info.audio_track_num > 0 then
    print("これは音声ファイルです")
    print("総再生時間: " .. info.total_time .. " 秒")
end
```

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
| `rate` | integer | 動画フレームレートの分子 |
| `scale` | integer | 動画フレームレートの分母 |
| `sample_rate` | integer | 音声サンプリングレート（Hz） |
| `project_path` | string または nil | プロジェクトファイルのパス、未保存の場合は nil |

実際のフレームレート（fps）は `rate / scale` で計算できます。

### 例

```lua
local project = gcmz.get_project_data()
print("解像度: " .. project.width .. "x" .. project.height)
print("フレームレート: " .. (project.rate / project.scale) .. " fps")
print("サンプリングレート: " .. project.sample_rate .. " Hz")
if project.project_path then
    print("プロジェクトパス: " .. project.project_path)
end
```

---

## gcmz.get_script_directory

GCMZDrops のスクリプトディレクトリのフルパスを取得します。

### 構文

```lua
local script_dir = gcmz.get_script_directory()
```

### パラメーター

なし。

### 戻り値

スクリプトディレクトリのフルパスを返します。

### 例

```lua
local script_dir = gcmz.get_script_directory()
print("スクリプトディレクトリ: " .. script_dir)

-- スクリプトディレクトリ内のファイルにアクセスする例
local config_path = script_dir .. "/config.ini"
local f = io.open(config_path, "r")
if f then
    local content = f:read("*a")
    f:close()
    -- content を処理...
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
| `filename` | string | 一時ファイルのベースファイル名 |

### 戻り値

成功時は一時ファイルへのフルパスを返します。失敗時は `nil, errmsg` を返します。

### エラー

- `filename` が指定されていない場合、エラーをスローします。
- 一時ファイルの作成に失敗した場合は `nil, errmsg` を返します。

### 例

```lua
local temp_path, err = gcmz.create_temp_file("output.txt")
if not temp_path then
    debug_print("一時ファイルの作成に失敗: " .. err)
    return
end
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
| `src_path` | string | ソースファイルのパス |
| `dest_filename` | string | 保存先のファイル名（フルパスではない） |

### 戻り値

成功時はファイルが保存された保存先のフルパスを返します。失敗時は `nil, errmsg` を返します。

### エラー

- `src_path` または `dest_filename` が指定されていない場合、エラーをスローします。
- ソースファイルの読み取りまたは保存先への書き込みに失敗した場合は `nil, errmsg` を返します。

### 例

```lua
local saved_path, err = gcmz.save_file("C:/temp/image.png", "saved_image.png")
if not saved_path then
    debug_print("ファイルの保存に失敗: " .. err)
    return
end
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

成功時は変換されたテキストを文字列として返します。失敗時は `nil, errmsg` を返します。

### エラー

- 必須パラメーターが指定されていない場合、エラーをスローします。
- エンコーディング名がサポートされていない場合、エラーをスローします。
- 変換に失敗した場合は `nil, errmsg` を返します。

### 例

```lua
-- UTF-8 から Shift_JIS に変換
local sjis_text, err = gcmz.convert_encoding("こんにちは", "utf8", "sjis")
if not sjis_text then
    debug_print("変換に失敗: " .. err)
    return
end

-- Shift_JIS から UTF-8 に変換
local utf8_text = gcmz.convert_encoding(sjis_text, "sjis", "utf8")
```

---

## gcmz.decode_exo_text

EXO テキストフィールド形式（16進数エンコードされた UTF-16LE）を文字列にデコードします。

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

成功時はデコードされたテキストを文字列として返します。失敗時は `nil, errmsg` を返します。

### エラー

- `hex_string` が指定されていない場合、エラーをスローします。
- 16進数文字列の長さが4の倍数でない場合、または無効な16進数文字が含まれている場合は `nil, errmsg` を返します。

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

# ini モジュール

## ini 概要

`ini` モジュールは、INI 形式の設定ファイルを解析、変更、シリアライズするための機能を提供します。

`require('ini')` で読み込むことで使用できます。

### 基本的な使い方

```lua
local ini = require('ini')

-- ファイルから読み込む
local config = ini.load("config.ini")

-- 値を取得
local value = config:get("section", "key", "default")

-- 値を設定
config:set("section", "key", "new_value")

-- ファイルに保存
config:save("config.ini")
```

### 空のインスタンスの作成

```lua
local ini = require('ini')

-- 空のインスタンスを作成
local config = ini.new()

-- 値を設定
config:set("section", "key", "value")

-- ファイルに保存
config:save("config.ini")
```

---

## ini.new

INI パーサーを作成します。

### 構文

```lua
local config = ini.new()
local config = ini.new(str)
local config = ini.new(iterator)
```

### パラメーター

| パラメーター | 型 | 説明 |
|-----------|------|-------------|
| `source` | nil / string / function | 入力ソース（省略可能） |

`source` には以下の値を指定できます：

| 値 | 説明 |
|---|------|
| `nil`（省略） | 空の INI オブジェクトを作成 |
| 文字列 | INI 形式の文字列として解析 |
| 関数 | 行イテレーター（例: `io.lines()`）として使用 |

### 戻り値

新しい INI パーサーオブジェクトを返します。

### 例

```lua
local ini = require('ini')

-- 空のインスタンスを作成
local config1 = ini.new()

-- 文字列から作成
local config2 = ini.new([=[
[section]
key1=value1
key2=value2
]=])

-- ファイルから読み込む（io.lines イテレーターを使用）
local config3 = ini.new(io.lines("config.ini"))

print(config2:get("section", "key1"))  -- 出力: value1
```

---

## ini.load

INI ファイルを読み込みます。

### 構文

```lua
local config = ini.load(filepath)
```

### パラメーター

| パラメーター | 型 | 説明 |
|-----------|------|-------------|
| `filepath` | string | 読み込む INI ファイルのパス |

### 戻り値

新しい INI パーサーオブジェクトを返します。

### 例

```lua
local ini = require('ini')
local config = ini.load("config.ini")
print(config:get("section", "key"))
```

---

## ini:save

INI オブジェクトをファイルに保存します。

### 構文

```lua
config:save(filepath)
```

### パラメーター

| パラメーター | 型 | 説明 |
|-----------|------|-------------|
| `filepath` | string | 保存先のファイルパス |

### エラー

- ファイルを開けない場合、エラーをスローします。

### 例

```lua
local ini = require('ini')
local config = ini.new()
config:set("section", "key", "value")
config:save("config.ini")
```

---

## ini:get

INI データから値を取得します。

### 構文

```lua
local value = config:get(sect, key, default)
```

### パラメーター

| パラメーター | 型 | 説明 |
|-----------|------|-------------|
| `sect` | string | セクション名 |
| `key` | string | キー名 |
| `default` | any | キーが存在しない場合に返すデフォルト値 |

### 戻り値

指定されたセクションとキーに対応する値を返します。キーが存在しない場合は `default` を返します。

### 例

```lua
local value = config:get("display", "width", "640")
print(value)  -- キーが存在すればその値、なければ "640"
```

---

## ini:set

INI データに値を設定します。

### 構文

```lua
config:set(sect, key, value)
```

### パラメーター

| パラメーター | 型 | 説明 |
|-----------|------|-------------|
| `sect` | string | セクション名 |
| `key` | string | キー名 |
| `value` | any | 設定する値（文字列に変換されます） |

### 説明

セクションやキーが存在しない場合は自動的に作成されます。

### 例

```lua
config:set("display", "width", 1920)
config:set("display", "height", 1080)
```

---

## ini:delete

INI データからキーを削除します。

### 構文

```lua
config:delete(sect, key)
```

### パラメーター

| パラメーター | 型 | 説明 |
|-----------|------|-------------|
| `sect` | string | セクション名 |
| `key` | string | 削除するキー名 |

### 例

```lua
config:delete("display", "width")
```

---

## ini:deletesection

INI データからセクション全体を削除します。

### 構文

```lua
config:deletesection(sect)
```

### パラメーター

| パラメーター | 型 | 説明 |
|-----------|------|-------------|
| `sect` | string | 削除するセクション名 |

### 例

```lua
config:deletesection("display")
```

---

## ini:sections

すべてのセクション名を取得します。

### 構文

```lua
local sections = config:sections()
```

### パラメーター

なし。

### 戻り値

セクション名の配列を返します。セクションは追加された順序で返されます。

### 例

```lua
local sections = config:sections()
for _, sect in ipairs(sections) do
    print("セクション: " .. sect)
end
```

---

## ini:keys

指定されたセクション内のすべてのキー名を取得します。

### 構文

```lua
local keys = config:keys(sect)
```

### パラメーター

| パラメーター | 型 | 説明 |
|-----------|------|-------------|
| `sect` | string | セクション名 |

### 戻り値

キー名の配列を返します。キーは追加された順序で返されます。セクションが存在しない場合は空のテーブルを返します。

### 例

```lua
local keys = config:keys("display")
for _, key in ipairs(keys) do
    print("キー: " .. key)
end
```

---

## ini:sectionexists

セクションが存在するかどうかを確認します。

### 構文

```lua
local exists = config:sectionexists(sect)
```

### パラメーター

| パラメーター | 型 | 説明 |
|-----------|------|-------------|
| `sect` | string | 確認するセクション名 |

### 戻り値

セクションが存在する場合は `true`、存在しない場合は `false` を返します。

### 例

```lua
if config:sectionexists("display") then
    print("display セクションが存在します")
end
```

---

## ini:exists

セクション内にキーが存在するかどうかを確認します。

### 構文

```lua
local exists = config:exists(sect, key)
```

### パラメーター

| パラメーター | 型 | 説明 |
|-----------|------|-------------|
| `sect` | string | セクション名 |
| `key` | string | 確認するキー名 |

### 戻り値

キーが存在する場合は `true`、存在しない場合は `false` を返します。

### 例

```lua
if config:exists("display", "width") then
    print("width キーが存在します")
end
```

---

## 文字列への変換

INI オブジェクトは `tostring()` を使って INI 形式の文字列に変換できます。

### 構文

```lua
local str = tostring(config)
```

### 説明

INI データを INI 形式の文字列に変換します。セクションとキーは追加された順序で出力されます。行末は CRLF（`\r\n`）です。

### 例

```lua
local ini = require('ini')
local config = ini.new("")
config:set("section1", "key1", "value1")
config:set("section1", "key2", "value2")
config:set("section2", "keyA", "valueA")

local str = tostring(config)
print(str)
-- 出力:
-- [section1]
-- key1=value1
-- key2=value2
-- [section2]
-- keyA=valueA
```

### ファイルへの保存

```lua
local f = io.open("output.ini", "wb")
if f then
    f:write(tostring(config))
    f:close()
end
```

---

# json モジュール

## json 概要

`json` モジュールは、JSON 形式のデータを解析（デコード）およびシリアライズ（エンコード）するための機能を提供します。

`require('json')` で読み込むことで使用できます。

### 基本的な使い方

```lua
local json = require('json')

-- JSON 文字列をパースする
local data = json.decode('{"name":"太郎","age":25}')
print(data.name)  -- "太郎"
print(data.age)   -- 25

-- Lua テーブルを JSON 文字列に変換する
local str = json.encode({name = "太郎", age = 25})
print(str)  -- {"name":"太郎","age":25}
```

### 対応するデータ型

JSON と Lua のデータ型は以下のように対応します：

| JSON | Lua |
|------|-----|
| object | table（文字列キー） |
| array | table（連番インデックス） |
| string | string |
| number | number |
| true/false | boolean |
| null | nil |

---

## json.decode

JSON 文字列を Lua の値に変換します。

### 構文

```lua
local value = json.decode(str)
```

### パラメーター

| パラメーター | 型 | 説明 |
|-----------|------|-------------|
| `str` | string | パースする JSON 文字列 |

### 戻り値

パースされた Lua の値を返します。JSON オブジェクトは Lua テーブル、JSON 配列は連番インデックスを持つ Lua テーブルになります。

### エラー

以下の場合にエラーをスローします：

- 引数が文字列でない場合
- JSON の構文が不正な場合
- 予期しない文字がある場合

エラーメッセージには行番号と列番号が含まれます。

### 例

```lua
local json = require('json')

-- オブジェクトのパース
local obj = json.decode('{"key": "value", "number": 42}')
print(obj.key)     -- "value"
print(obj.number)  -- 42

-- 配列のパース
local arr = json.decode('[1, 2, 3, "four"]')
print(arr[1])  -- 1
print(arr[4])  -- "four"

-- ネストした構造
local nested = json.decode('{"items": [{"id": 1}, {"id": 2}]}')
print(nested.items[1].id)  -- 1

-- null は nil になる
local with_null = json.decode('{"value": null}')
print(with_null.value)  -- nil

-- ファイルから読み込む
local f = io.open("data.json", "rb")
if f then
    local content = f:read("*a")
    f:close()
    local data = json.decode(content)
    -- data を使用...
end
```

---

## json.encode

Lua の値を JSON 文字列に変換します。

### 構文

```lua
local str = json.encode(value)
```

### パラメーター

| パラメーター | 型 | 説明 |
|-----------|------|-------------|
| `value` | any | JSON に変換する Lua の値 |

### 戻り値

JSON 形式の文字列を返します。

### エラー

以下の場合にエラーをスローします：

- 循環参照がある場合
- テーブルのキーが文字列と数値で混在している場合
- 配列が疎（sparse）な場合（連続しない数値インデックス）
- NaN、無限大などの特殊な数値の場合
- 関数やユーザーデータなどエンコード不可能な型の場合

### 配列とオブジェクトの判定

テーブルは以下のルールで配列またはオブジェクトとして判定されます：

- インデックス 1 に値がある、または空のテーブルの場合は配列として扱われます
- それ以外は文字列キーを持つオブジェクトとして扱われます

### 例

```lua
local json = require('json')

-- オブジェクトのエンコード
local str = json.encode({name = "花子", score = 100})
print(str)  -- {"name":"花子","score":100}

-- 配列のエンコード
local arr_str = json.encode({1, 2, 3, "four"})
print(arr_str)  -- [1,2,3,"four"]

-- ネストした構造
local nested_str = json.encode({
    users = {
        {id = 1, name = "Alice"},
        {id = 2, name = "Bob"}
    }
})
print(nested_str)

-- nil/boolean のエンコード
print(json.encode(nil))    -- null
print(json.encode(true))   -- true
print(json.encode(false))  -- false

-- ファイルに保存
local data = {version = "1.0", settings = {volume = 80}}
local f = io.open("config.json", "wb")
if f then
    f:write(json.encode(data))
    f:close()
end
```

### 注意事項

- 出力される JSON はコンパクト形式（余分な空白なし）です
- オブジェクトのキー順序は保証されません
- 文字列はそのまま出力されます
- 制御文字は適切にエスケープされます
