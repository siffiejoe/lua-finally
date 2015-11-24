local pcall, error = pcall, error

local function _finally( after, ok, ... )
  after()
  if ok then
    return ...
  else
    error( (...), 0 )
  end
end

local function finally( main, after )
  return _finally( after, pcall( main ) )
end

return finally

