--- GCMZDrops2 entrypoint module.
-- Handles drag_enter, drag_leave, and drop events by delegating to registered modules.
-- @module entrypoint

local M = {}

-- Local module storage (not accessible from global scope)
local modules = {}

--- Load a single handler script and register it as a module.
-- @param filepath string Path to the script file
-- @return boolean true on success, false on failure
local function load_handler(filepath)
  local chunk, err = loadfile(filepath)
  if not chunk then
    debug_print("failed to load handler: " .. filepath .. ": " .. tostring(err))
    return false
  end
  local ok, module_table = pcall(chunk)
  if not ok then
    debug_print("failed to execute handler: " .. filepath .. ": " .. tostring(module_table))
    return false
  end
  if type(module_table) ~= "table" then
    debug_print("handler must return a table: " .. filepath)
    return false
  end
  local priority = module_table.priority
  if type(priority) ~= "number" then
    -- Not a handler module (no priority), skip silently
    return true
  end
  table.insert(modules, {
    name = "@" .. filepath,
    priority = priority,
    module = module_table,
    active = true,
  })
  return true
end

--- Load handler scripts from a list of file paths.
-- Called from C side with the list of .lua files in the script directory.
-- Loads all scripts, registers them as modules, and sorts by priority.
-- @param filepaths table Array of file paths to load
function M.load_handlers(filepaths)
  if type(filepaths) ~= "table" then
    return
  end
  for _, filepath in ipairs(filepaths) do
    load_handler(filepath)
  end
  table.sort(modules, function(a, b)
    return a.priority < b.priority
  end)
end

--- Add a handler module from a table.
-- Checks for priority field and registers the module if valid.
-- @param name string Module name for identification
-- @param module_table table The module table returned by the script
-- @return boolean true on success (including skip for non-handler modules)
-- @local
local function add_module(name, module_table)
  if type(name) ~= "string" or type(module_table) ~= "table" then
    return false
  end
  local priority = module_table.priority
  if type(priority) ~= "number" then
    -- Not a handler module (no priority), skip silently
    return true
  end
  table.insert(modules, {
    name = name,
    priority = priority,
    module = module_table,
    active = true,
  })
  table.sort(modules, function(a, b)
    return a.priority < b.priority
  end)
  return true
end

--- Add a handler module from a script string.
-- Loads and executes the script, then registers it as a module if valid.
-- @param name string Module name for identification (also used as chunk name)
-- @param script string The script content to execute
-- @return boolean, string true on success, or false and error message on failure
function M.add_module_from_string(name, script)
  if type(name) ~= "string" or type(script) ~= "string" then
    return false, "invalid arguments"
  end
  local chunk, err = loadstring(script, name)
  if not chunk then
    return false, err
  end
  local ok, module_table = pcall(chunk)
  if not ok then
    return false, module_table
  end
  if type(module_table) ~= "table" then
    return false, "handler script must return a table"
  end
  return add_module(name, module_table), nil
end

--- Add a handler module from a file.
-- Loads and executes the script file, then registers it as a module if valid.
-- @param filepath string Path to the script file
-- @return boolean, string true on success, or false and error message on failure
function M.add_module_from_file(filepath)
  if type(filepath) ~= "string" then
    return false, "invalid arguments"
  end
  local chunk, err = loadfile(filepath)
  if not chunk then
    return false, err
  end
  local ok, module_table = pcall(chunk)
  if not ok then
    return false, module_table
  end
  if type(module_table) ~= "table" then
    return false, "handler script must return a table"
  end
  return add_module("@" .. filepath, module_table), nil
end

--- Get the number of registered modules.
-- Used for testing purposes.
-- @return number The number of registered modules
function M.get_module_count()
  return #modules
end

--- Get a module entry by index.
-- Used for testing purposes.
-- @param index number 1-based index
-- @return table|nil The module entry or nil if out of range
function M.get_module(index)
  return modules[index]
end

--- Call drag_enter hook on all loaded modules in priority order.
-- Modules that return false from drag_enter are marked as inactive.
-- If a handler throws an error, it is caught and logged, and the handler is marked inactive.
-- @param files table File list with format { {filepath="...", mimetype="...", temporary=bool}, ... }
-- @param state table Key state with format { control=bool, shift=bool, alt=bool, ... }
-- @return table The files table (possibly modified by modules)
function M.drag_enter(files, state)
  -- Reset all module active flags to true
  for _, entry in ipairs(modules) do
    entry.active = true
  end

  -- Call drag_enter on each module
  for _, entry in ipairs(modules) do
    if entry.active and entry.module and entry.module.drag_enter then
      local ok, result = pcall(entry.module.drag_enter, files, state)
      if not ok then
        debug_print("error in " .. entry.name .. ".drag_enter: " .. tostring(result))
        entry.active = false
      elseif result == false then
        entry.active = false
      end
    end
  end

  return files
end

--- Call drag_leave hook on all active modules in priority order.
-- If a handler throws an error, it is caught and logged.
function M.drag_leave()
  for _, entry in ipairs(modules) do
    if entry.active and entry.module and entry.module.drag_leave then
      local ok, err = pcall(entry.module.drag_leave)
      if not ok then
        debug_print("error in " .. entry.name .. ".drag_leave: " .. tostring(err))
      end
    end
  end
end

--- Call drop hook on all active modules in priority order.
-- If a handler throws an error, it is caught and logged, but processing continues.
-- @param files table File list with format { {filepath="...", mimetype="...", temporary=bool}, ... }
-- @param state table Key state with format { control=bool, shift=bool, alt=bool, ... }
-- @return table The files table (possibly modified by modules)
function M.drop(files, state)
  for _, entry in ipairs(modules) do
    if entry.active and entry.module and entry.module.drop then
      local ok, err = pcall(entry.module.drop, files, state)
      if not ok then
        debug_print("error in " .. entry.name .. ".drop: " .. tostring(err))
      end
    end
  end

  return files
end

--- Convert EXO files to object format.
-- Calls the exo module's process_file_list function to convert AviUtl1 .exo files
-- to AviUtl2 .object files.
-- If an error occurs, it is caught and logged, and the original files are returned.
-- @param files table File list with format { {filepath="...", mimetype="...", temporary=bool}, ... }
-- @return table The files table (with .exo files converted to .object files)
function M.exo_convert(files)
  local ok, result = pcall(function()
    return require("exo").process_file_list(files)
  end)
  if not ok then
    debug_print("error in exo_convert: " .. tostring(result))
    return files
  end
  return result
end

return M
