-- Test Lua script with emoji in filename
return {
  message = "Hello from Unicode Lua script! 🌙",
  emoji = "🌙",
  test = function()
    return "test passed"
  end
}
