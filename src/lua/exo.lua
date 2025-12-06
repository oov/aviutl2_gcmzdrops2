--- EXO to Object converter module.
-- Converts AviUtl1 .exo files (Shift_JIS) to AviUtl ExEdit2 .object files (UTF-8).
--
-- NOTE: This module does NOT implement drag_enter/drop hooks.
-- It is called explicitly from C code only when the use_exo_converter flag is enabled.
-- @module exo
local ini = require("ini")

local M = {}

--- Effect conversion tables.
-- Each effect table defines how to convert AviUtl1 effects to AviUtl2 format.
-- Table structure:
--   - name: output effect name (if different from input)
--   - props: property mapping table { exo_key = { key = "object_key", default = value } }
--   - defaults: properties to add with default values if not present in source
--   - transform: optional function(props, exo_props) to do custom transformation
-- @local

local effect_tables = {}

--- Audio file effect conversion table.
-- Converts 音声ファイル effect from AviUtl1 to AviUtl2 format.
-- @local
effect_tables["音声ファイル"] = {
  props = {
    ["再生位置"] = { key = "再生位置", decimals = 3 },
    ["再生速度"] = { key = "再生速度", decimals = 2 },
    ["file"] = { key = "ファイル" },
    ["ループ再生"] = { key = "ループ再生" },
  },
  defaults = {
    ["トラック"] = "0",
  },
}

--- Standard playback to audio playback conversion.
-- Converts 標準再生 effect to 音声再生 effect.
-- @local
effect_tables["標準再生"] = {
  name = "音声再生",
  props = {
    ["音量"] = { key = "音量", decimals = 2 },
    ["左右"] = { key = "左右", decimals = 2 },
  },
}

--- Text effect conversion table.
-- Converts テキスト effect with comprehensive property mapping.
-- @local
effect_tables["テキスト"] = {
  props = {
    ["サイズ"] = { key = "サイズ", decimals = 2 },
    ["表示速度"] = { key = "表示速度", decimals = 2 },
    ["font"] = { key = "フォント" },
    ["color"] = { key = "文字色" },
    ["color2"] = { key = "影・縁色" },
    ["B"] = { key = "B" },
    ["I"] = { key = "I" },
    ["text"] = { key = "テキスト", transform = "decode_exo_text" },
    ["文字毎に個別オブジェクト"] = { key = "文字毎に個別オブジェクト" },
    ["自動スクロール"] = { key = "自動スクロール" },
    ["移動座標上に表示"] = { key = "移動座標上に表示" },
    ["spacing_x"] = { key = "字間", decimals = 2 },
    ["spacing_y"] = { key = "行間", decimals = 2 },
  },
  defaults = {
    ["サイズ"] = "40.00",
    ["字間"] = "0.00",
    ["行間"] = "0.00",
    ["表示速度"] = "0.00",
    ["フォント"] = "Yu Gothic UI",
    ["文字色"] = "ffffff",
    ["影・縁色"] = "000000",
    ["文字装飾"] = "標準文字",
    ["文字揃え"] = "左寄せ[上]",
    ["B"] = "0",
    ["I"] = "0",
    ["テキスト"] = "",
    ["文字毎に個別オブジェクト"] = "0",
    ["自動スクロール"] = "0",
    ["移動座標上に表示"] = "0",
    ["オブジェクトの長さを自動調節"] = "0",
  },
}

--- Image file effect conversion table.
-- Converts 画像ファイル effect for image file handling.
-- @local
effect_tables["画像ファイル"] = {
  props = {
    ["file"] = { key = "ファイル" },
  },
  defaults = {
    ["表示番号"] = "0",
    ["連番ファイル"] = "0",
  },
}

--- Standard drawing effect conversion table.
-- Converts 標準描画 effect with position, scale, and transparency properties.
-- Includes custom transformation for blend modes and rotation.
-- @local
effect_tables["標準描画"] = {
  props = {
    ["X"] = { key = "X", decimals = 2 },
    ["Y"] = { key = "Y", decimals = 2 },
    ["Z"] = { key = "Z", decimals = 2 },
    ["拡大率"] = { key = "拡大率", decimals = 3 },
    ["透明度"] = { key = "透明度", decimals = 2 },
  },
  defaults = {
    ["X"] = "0.00",
    ["Y"] = "0.00",
    ["Z"] = "0.00",
    ["Group"] = "1",
    ["中心X"] = "0.00",
    ["中心Y"] = "0.00",
    ["中心Z"] = "0.00",
    ["X軸回転"] = "0.00",
    ["Y軸回転"] = "0.00",
    ["Z軸回転"] = "0.00",
    ["拡大率"] = "100.000",
    ["縦横比"] = "0.000",
    ["透明度"] = "0.00",
    ["合成モード"] = "通常",
  },
  --- Custom transformation function for standard drawing effect.
  -- Converts blend mode values and rotation values to AviUtl2 format.
  -- @param out_props table Output properties table to modify
  -- @param exo_props table Input EXO properties table
  -- @local
  transform = function(out_props, exo_props)
    -- blend value to 合成モード
    local blend = exo_props["blend"]
    if blend then
      local blend_modes = {
        ["0"] = "通常",
        ["1"] = "加算",
        ["2"] = "減算",
        ["3"] = "乗算",
        ["4"] = "スクリーン",
        ["5"] = "オーバーレイ",
        ["6"] = "比較(明)",
        ["7"] = "比較(暗)",
      }
      out_props["合成モード"] = blend_modes[blend] or "通常"
    end
    -- 回転 to Z軸回転
    local rotation = exo_props["回転"]
    if rotation then
      out_props["Z軸回転"] = rotation
    end
  end,
}

--- Format number with specified decimal places.
-- Converts a numeric value to a string with the specified number of decimal places.
-- @param value string|number The value to format
-- @param decimals number Number of decimal places
-- @return string|nil Formatted number string, or original value if not numeric, or nil if input is nil
-- @local
local function format_number(value, decimals)
  if not value then
    return nil
  end
  local num = tonumber(value)
  if not num then
    return value
  end
  return string.format("%." .. decimals .. "f", num)
end

--- Convert a single effect section.
-- Processes one effect section from the EXO file and converts it to AviUtl2 format.
-- @param exo table The parsed EXO file as an INI object
-- @param section_name string The name of the effect section to convert
-- @return string|nil, table|nil Effect name and properties table, or nil if conversion fails
-- @local
local function convert_effect(exo, section_name)
  local effect_name = exo:get(section_name, "_name")
  if not effect_name then
    return nil
  end

  local table_def = effect_tables[effect_name]
  if not table_def then
    error(string.format("unsupported effect: %s (section: %s)", effect_name, section_name))
  end

  local out_name = table_def.name or effect_name
  local out_props = {}

  -- Start with defaults
  if table_def.defaults then
    for k, v in pairs(table_def.defaults) do
      out_props[k] = v
    end
  end

  -- Apply property mappings
  if table_def.props then
    for exo_key, mapping in pairs(table_def.props) do
      local value = exo:get(section_name, exo_key)
      if value then
        -- Apply transformation if specified
        if mapping.transform == "decode_exo_text" then
          value = gcmz.decode_exo_text(value)
        elseif mapping.decimals then
          value = format_number(value, mapping.decimals)
        end
        out_props[mapping.key] = value
      end
    end
  end

  -- Apply custom transform function (only build exo_props if needed)
  if table_def.transform then
    local exo_props = {}
    for _, key in ipairs(exo:keys(section_name)) do
      exo_props[key] = exo:get(section_name, key)
    end
    table_def.transform(out_props, exo_props)
  end

  return out_name, out_props
end

--- Convert EXO content to object format.
-- Takes the raw content of an EXO file and converts it to AviUtl2 object format.
-- Handles encoding conversion, INI parsing, and effect conversion.
-- @param exo_content string Raw EXO file content in Shift_JIS encoding
-- @return string Converted object file content in UTF-8
-- @raise error if encoding conversion, parsing, or effect conversion fails
-- @local
local function convert_exo_to_object(exo_content)
  -- Convert from Shift_JIS to UTF-8
  local utf8_content = gcmz.convert_encoding(exo_content, "sjis", "utf8")
  if not utf8_content then
    error("Failed to convert encoding from Shift_JIS to UTF-8")
  end

  -- Parse INI
  local exo = ini.new(utf8_content)
  if not exo then
    error("Failed to parse EXO content as INI format")
  end

  -- Output INI
  local out = ini.new()
  local obj_idx = 0

  while true do
    local section = tostring(obj_idx)
    local start_val = exo:get(section, "start")
    if not start_val then
      break
    end

    local end_val = exo:get(section, "end")
    local layer_val = exo:get(section, "layer")
    local group_val = exo:get(section, "group")

    if start_val and end_val and layer_val then
      local start_frame = tonumber(start_val) - 1
      local end_frame = tonumber(end_val) - 1
      local layer = tonumber(layer_val) - 1

      -- Use same index for output section
      local out_section = section

      out:set(out_section, "layer", tostring(layer))
      out:set(out_section, "frame", start_frame .. "," .. end_frame)
      if group_val then
        out:set(out_section, "group", group_val)
      end

      -- Process effect sub-sections [0.0], [0.1], etc.
      local effect_idx = 0
      local out_effect_idx = 0
      while true do
        local effect_section = section .. "." .. effect_idx
        if not exo:sectionexists(effect_section) then
          break
        end

        local out_name, out_props = convert_effect(exo, effect_section)
        if out_name and out_props then
          local out_effect_section = out_section .. "." .. out_effect_idx
          out:set(out_effect_section, "effect.name", out_name)
          for key, value in pairs(out_props) do
            out:set(out_effect_section, key, value)
          end
          out_effect_idx = out_effect_idx + 1
        end

        effect_idx = effect_idx + 1
      end
    end

    obj_idx = obj_idx + 1
  end

  return tostring(out)
end

--- Process a single EXO file entry and convert it to object format.
-- Converts an EXO file to a temporary object file if the file has .exo extension.
-- The file entry is modified in-place with the new temporary file path.
-- @param file table File entry with filepath, mimetype, and other properties
-- @local
local function process_exo_file_entry(file)
  local filepath = file.filepath
  if not filepath then
    return
  end
  if not filepath:match("%.exo$") then
    return
  end

  -- Read file content
  local f = io.open(filepath, "rb")
  if not f then
    return
  end
  local content = f:read("*a")
  f:close()
  if not content then
    return
  end

  -- Convert EXO file
  local success, object_content = pcall(convert_exo_to_object, content)
  if not success then
    return
  end
  if not object_content then
    return
  end

  -- Create temp file
  local basename = filepath:match("([^/\\]+)$") or "converted.exo"
  local temp_filename = basename:gsub("%.exo$", "") .. ".object"

  local temp_path = gcmz.create_temp_file(temp_filename)
  if not temp_path then
    return
  end

  -- Write content to temp file
  local out = io.open(temp_path, "wb")
  if not out then
    return
  end
  local ok = out:write(object_content)
  out:close()

  if ok then
    -- Update file entry
    file.filepath = temp_path
    file.mimetype = "application/aviutl-object"
    file.temporary = true
  end
end

--- Process file list and convert EXO files to object files.
-- Iterates through a list of files and converts any EXO files to temporary object files.
-- The original file entries are modified in-place to point to the converted files.
-- @param files table List of files with format { {filepath="...", mimetype="..."}, ... }
-- @return table The same file list (modified in-place, but returned for convenience)
-- @usage local converted_files = exo.process_file_list(files)
function M.process_file_list(files)
  for i, file in ipairs(files) do
    process_exo_file_entry(file)
  end
  return files
end

return M
