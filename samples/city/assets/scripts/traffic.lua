-- Traffic around the player (docs/design/0025).
--
-- The engine's TrafficSystem drives the cars: the metro world's streets are a grid every
-- 32 m along X and Z, 8 m wide, with right-hand lanes 2 m from the centre line and turns
-- through each crossing. Cars follow the car ahead, take turns through crossings, stop for
-- the player, the player's car and pedestrians (a ray on the moving layer), and are
-- kinematic bodies the player's car collides with. This module keeps the game's small
-- interface (new, update, stats, clear) over the engine's `traffic` library.

local Traffic = {}
Traffic.__index = Traffic

local MODELS = { "models/car_blue.gltf", "models/car_white.gltf", "models/car_yellow.gltf" }
-- Street lines at multiples of 32 m inside the 4 km world.
local GRID = { origin_x = -1984.0, origin_z = -1984.0, columns = 125, rows = 125, spacing = 32.0,
  half_width = 4.0, lane_offset = 2.0, height = 0.73, speed = 11.0 }

function Traffic.new(count, radius)
  local self = setmetatable({ count = count or 10, radius = radius or 140.0, avoid_x = nil, cleared = false }, Traffic)
  if self.count > 0 then
    traffic.start({ models = MODELS, count = self.count, radius = self.radius, grid = GRID, seed = 1 })
  else
    traffic.stop()
  end
  return self
end

function Traffic:update(dt, focus)
  -- A street (world X) to keep clear for the scripted test run: no car appears on it.
  if self.avoid_x and not self.cleared then
    self.cleared = true
    traffic.keep_clear(vec3(self.avoid_x, 0.0, 790.0), 70.0)
  end
end

-- How many cars there are, and how far they have driven in total (metres).
function Traffic:stats()
  return traffic.stats()
end

function Traffic:clear()
  traffic.stop()
end

return Traffic
