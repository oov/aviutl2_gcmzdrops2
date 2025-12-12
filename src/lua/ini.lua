--- INI file parser module.
-- Provides functionality to parse, modify, and serialize INI format configuration files.
-- @module ini
-- @usage local ini = require('ini')
-- local config = ini.load("config.ini")
-- local value = config:get("section", "key", "default")
-- config:set("section", "key", "new_value")
-- ini.save(config, "config.ini")
local P = {}

--- Create a new INI parser.
-- Creates an INI parser from various input types.
-- @param source string|function|nil The source to parse:
--   - nil: Creates an empty INI object
--   - string: Parses the string as INI format content
--   - function: Uses as a line iterator (e.g., io.lines())
-- @return table A new INI parser object.
-- @usage -- Create empty instance
-- local config = ini.new()
-- @usage -- From string
-- local config = ini.new("[section]\nkey=value")
-- @usage -- From file using io.lines iterator
-- local config = ini.new(io.lines("config.ini"))
function P.new(source)
  if source == nil then
    return P.new_core(function()
      return nil
    end)
  end
  local t = type(source)
  if t == "function" then
    return P.new_core(source)
  end
  return P.new_core(tostring(source):gmatch("[^\r\n]+"))
end

--- Load an INI file.
-- Reads and parses an INI file from the specified path.
-- @param filepath string The path to the INI file to load.
-- @return table A new INI parser object.
-- @usage local config = ini.load("config.ini")
function P.load(filepath)
  return P.new(io.lines(tostring(filepath)))
end

--- Save the INI object to a file.
-- Writes the INI data to the specified file path.
-- @param filepath string The path to the file to write.
-- @usage config:save("config.ini")
function P:save(filepath)
  local f = io.open(tostring(filepath), "wb")
  if not f then
    error("failed to open file: " .. tostring(filepath))
  end
  f:write(tostring(self))
  f:close()
end

--- Internal function to create a new INI parser from a line iterator.
-- @param iter function A line iterator function.
-- @return table A new INI parser object.
-- @local
function P.new_core(iter)
  local o = setmetatable({ data = {}, idx = 0 }, { __index = P, __tostring = P.__tostring })
  local sect = ""
  for line in iter do
    local m = line:match("^%[([^%]]+)%]$")
    if m ~= nil then
      sect = m
    else
      local key, value = line:match("^([^=]+)=(.*)$")
      if key ~= nil then
        o:set(sect, key, value)
      end
    end
  end
  return o
end

--- Convert the INI object to a string.
-- Serializes the INI data back to INI format string.
-- Sections and keys are output in the order they were added.
-- @return string The INI format string with CRLF line endings.
-- @usage local str = tostring(config)
function P:__tostring()
  local sects = {}
  for sect, t in pairs(self.data) do
    local values = {}
    for k, v in pairs(t.t) do
      table.insert(values, v)
    end
    table.sort(values, function(a, b)
      return (a.idx < b.idx)
    end)
    table.insert(sects, { key = sect, idx = t.idx, values = values })
  end
  table.sort(sects, function(a, b)
    return (a.idx < b.idx)
  end)
  local r = {}
  for i, sect in ipairs(sects) do
    table.insert(r, "[" .. sect.key .. "]")
    for j, value in ipairs(sect.values) do
      table.insert(r, value.key .. "=" .. value.v)
    end
  end
  return table.concat(r, "\r\n") .. "\r\n"
end

--- Get a value from the INI data.
-- Retrieves the value associated with the specified section and key.
-- @param sect string The section name.
-- @param key string The key name.
-- @param default any The default value to return if the key does not exist.
-- @return string|any The value if found, otherwise the default value.
-- @usage local value = config:get("section", "key", "default")
function P:get(sect, key, default)
  sect = tostring(sect)
  key = tostring(key)
  if (self.data[sect] ~= nil) and (self.data[sect].t[key] ~= nil) then
    return self.data[sect].t[key].v
  end
  return default
end

--- Set a value in the INI data.
-- Sets or updates the value for the specified section and key.
-- If the section or key does not exist, they will be created.
-- @param sect string The section name.
-- @param key string The key name.
-- @param value any The value to set (will be converted to string).
-- @usage config:set("section", "key", "value")
function P:set(sect, key, value)
  sect = tostring(sect)
  key = tostring(key)
  if self.data[sect] == nil then
    self.idx = self.idx + 1
    self.data[sect] = { sect = sect, idx = self.idx, t = {} }
  end
  if self.data[sect].t[key] == nil then
    self.idx = self.idx + 1
    self.data[sect].t[key] = { key = key, idx = self.idx }
  end
  self.data[sect].t[key].v = tostring(value)
end

--- Delete a key from the INI data.
-- Removes the specified key from the given section.
-- @param sect string The section name.
-- @param key string The key name to delete.
-- @usage config:delete("section", "key")
function P:delete(sect, key)
  sect = tostring(sect)
  key = tostring(key)
  if self.data[sect] ~= nil then
    self.data[sect].t[key] = nil
  end
end

--- Delete an entire section from the INI data.
-- Removes the specified section and all its keys.
-- @param sect string The section name to delete.
-- @usage config:deletesection("section")
function P:deletesection(sect)
  sect = tostring(sect)
  self.data[sect] = nil
end

--- Get a list of all section names.
-- Returns all section names in the order they were added.
-- @return table An array of section names.
-- @usage local sections = config:sections()
-- for _, sect in ipairs(sections) do print(sect) end
function P:sections()
  local sects = {}
  for sect, t in pairs(self.data) do
    table.insert(sects, t)
  end
  table.sort(sects, function(a, b)
    return (a.idx < b.idx)
  end)
  local r = {}
  for i, v in ipairs(sects) do
    table.insert(r, v.sect)
  end
  return r
end

--- Get a list of all keys in a section.
-- Returns all key names in the specified section in the order they were added.
-- @param sect string The section name.
-- @return table An array of key names. Returns an empty table if the section does not exist.
-- @usage local keys = config:keys("section")
-- for _, key in ipairs(keys) do print(key) end
function P:keys(sect)
  sect = tostring(sect)
  local r = {}
  if self.data[sect] ~= nil then
    local values = {}
    for key, v in pairs(self.data[sect].t) do
      table.insert(values, v)
    end
    table.sort(values, function(a, b)
      return (a.idx < b.idx)
    end)
    for i, v in ipairs(values) do
      table.insert(r, v.key)
    end
  end
  return r
end

--- Check if a section exists.
-- @param sect string The section name to check.
-- @return boolean True if the section exists, false otherwise.
-- @usage if config:sectionexists("section") then ... end
function P:sectionexists(sect)
  sect = tostring(sect)
  return self.data[sect] ~= nil
end

--- Check if a key exists in a section.
-- @param sect string The section name.
-- @param key string The key name to check.
-- @return boolean True if the key exists in the section, false otherwise.
-- @usage if config:exists("section", "key") then ... end
function P:exists(sect, key)
  sect = tostring(sect)
  key = tostring(key)
  return (self.data[sect] ~= nil) and (self.data[sect].t[key] ~= nil)
end

return P
