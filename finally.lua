local pcall, error = pcall, error

local function _finally( after, ok, ... )
  if ok then
    after()
    return ...
  else
    after( (...) )
    error( (...), 0 )
  end
end

local function finally( main, after )
  return _finally( after, pcall( main ) )
end

return finally

