-- Traffic around the player (ROADMAP M6).
--
-- The metro world's streets are a grid: every 32 m along X and Z, 8 m wide. Cars drive
-- on the right-hand lane (2 m from the centre line), choose straight on, left or right at
-- each crossing, and turn along a curve. A ray ahead of each car stops it behind anything
-- on the moving layer: another car, the player's car or the player. Each car is a
-- kinematic body, so the player's car collides with it.
--
-- Positions are kept in world coordinates (doubles in Lua) and turned into scene
-- coordinates every frame, so origin rebasing never disturbs a route.

local Traffic = {}
Traffic.__index = Traffic

local GRID = 32.0
local LANE = 2.0
local RIDE_HEIGHT = 0.73
local MODELS = { "models/car_blue.gltf", "models/car_white.gltf", "models/car_yellow.gltf" }
local WORLD_HALF = 1990.0

local function right_of(d) return vec3(-d.z, 0.0, d.x) end
local function snap(v) return math.floor(v / GRID + 0.5) * GRID end
local function yaw_of(d) return math.deg(math.atan(-d.x, -d.z)) end

function Traffic.new(count, radius)
  return setmetatable({
    count = count or 10,
    radius = radius or 140.0,
    cars = {},
    timer = 0.0,
    spawned = 0,
    avoid_x = nil, -- a street (world X) to keep clear, for the scripted test run
  }, Traffic)
end

function Traffic:spawn(focus)
  local f = scene.to_world(focus)
  local dirs = { vec3(1, 0, 0), vec3(-1, 0, 0), vec3(0, 0, 1), vec3(0, 0, -1) }
  local dir = dirs[math.random(1, 4)]
  local cx = snap(f.x + (math.random() * 2.0 - 1.0) * self.radius * 0.8)
  local cz = snap(f.z + (math.random() * 2.0 - 1.0) * self.radius * 0.8)
  local pos = vec3(cx, RIDE_HEIGHT, cz) + dir * (GRID * 0.5) + right_of(dir) * LANE
  local distance = vec3.distance(vec3(pos.x, 0, pos.z), vec3(f.x, 0, f.z))
  if distance < 40.0 or distance > self.radius or math.abs(pos.x) > WORLD_HALF or math.abs(pos.z) > WORLD_HALF then
    return
  end
  if self.avoid_x and dir.z ~= 0 and math.abs(cx - self.avoid_x) < 1.0 then
    return
  end
  for _, car in ipairs(self.cars) do
    if vec3.distance(car.pos, pos) < 14.0 then return end
  end
  self.spawned = self.spawned + 1
  local object = scene.spawn(MODELS[(self.spawned % #MODELS) + 1], scene.to_scene(pos), yaw_of(dir))
  object:set_name("Traffic" .. self.spawned)
  object:add("PhysicsComponent", { bodyType = "kinematic", colliderType = "box", size = vec3(1.8, 1.3, 4.2) })
  table.insert(self.cars, {
    object = object,
    pos = pos,
    start = vec3.copy(pos),
    travelled = 0.0,
    dir = dir,
    speed = 0.0,
    cruise = 8.0 + math.random() * 3.0,
    decided_for = nil,
    turn = nil,
    blocked = 0.0,
  })
end

local function bezier(a, p, b, t)
  local u = 1.0 - t
  return a * (u * u) + p * (2.0 * u * t) + b * (t * t)
end

local function bezier_tangent(a, p, b, t)
  return (p - a) * (2.0 * (1.0 - t)) + (b - p) * (2.0 * t)
end

-- At the next crossing, decide: straight on, or a curve onto the crossing street.
function Traffic:plan(car)
  local d = car.dir
  -- The centre of the next crossing ahead.
  local along = d.x ~= 0 and car.pos.x or car.pos.z
  local sign = d.x ~= 0 and d.x or d.z
  local next_line = sign > 0 and math.floor(along / GRID + 1.0) * GRID or math.ceil(along / GRID - 1.0) * GRID
  if math.abs(next_line - along) < 0.5 then next_line = next_line + sign * GRID end
  local centre = d.x ~= 0 and vec3(next_line, RIDE_HEIGHT, snap(car.pos.z)) or vec3(snap(car.pos.x), RIDE_HEIGHT, next_line)
  if math.abs(centre.x) > WORLD_HALF or math.abs(centre.z) > WORLD_HALF then
    car.turn = nil
    car.decided_for = centre
    car.reverse_at = centre
    return
  end
  local roll = math.random()
  car.decided_for = centre
  if roll < 0.55 then
    car.turn = nil
    return
  end
  local r = right_of(d)
  local e = roll < 0.78 and r or -r
  local radius = (e == r) and 3.5 or 5.5
  local corner = centre + r * LANE + right_of(e) * LANE
  car.turn = {
    a = corner - d * radius,
    p = corner,
    b = corner + e * radius,
    exit = e,
    t = -1.0, -- not started
    length = radius * 1.6,
  }
end

function Traffic:drive(car, dt)
  local object = car.object
  local ahead = car.turn and car.turn.t >= 0.0
      and bezier_tangent(car.turn.a, car.turn.p, car.turn.b, car.turn.t):normalized() or car.dir
  -- Anything ahead on the moving layer: slow down and stop behind it.
  local front = scene.to_scene(car.pos + ahead * 2.4 + vec3(0, 0.0, 0))
  local hit = physics.raycast(front, ahead, 11.0, "moving", object)
  local target = car.cruise
  if hit and car.blocked < 4.0 then
    target = hit.distance < 4.0 and 0.0 or car.cruise * (hit.distance - 4.0) / 7.0
  end
  if car.turn and car.turn.t >= 0.0 then
    target = math.min(target, 5.5)
  end
  if car.speed < 0.3 and target < 0.3 then
    car.blocked = car.blocked + dt
  elseif car.blocked >= 4.0 then
    -- Waited long enough (two cars at a crossing): creep on regardless for a moment.
    car.blocked = car.blocked + dt
    if car.blocked > 6.0 then car.blocked = 0.0 end
  else
    car.blocked = 0.0
  end
  local rate = target > car.speed and 3.0 or 9.0
  car.speed = car.speed + math.clamp(target - car.speed, -rate * dt, rate * dt)
  local step = car.speed * dt
  car.travelled = car.travelled + step

  if car.reverse_at and vec3.distance(car.pos, car.reverse_at) < 6.0 then
    -- The edge of the world: turn around.
    car.dir = -car.dir
    car.pos = car.pos + right_of(car.dir) * (LANE * 2.0)
    car.reverse_at = nil
    car.decided_for = nil
  end

  if not car.decided_for then
    self:plan(car)
  end
  local turn = car.turn
  if turn then
    if turn.t < 0.0 then
      -- Straight towards the curve's start.
      car.pos = car.pos + car.dir * step
      if vec3.dot(car.pos - turn.a, car.dir) >= 0.0 then
        turn.t = 0.0
      end
    else
      turn.t = turn.t + step / turn.length
      if turn.t >= 1.0 then
        car.pos = turn.b + turn.exit * ((turn.t - 1.0) * turn.length)
        car.dir = turn.exit
        car.turn = nil
        car.decided_for = nil
      else
        car.pos = bezier(turn.a, turn.p, turn.b, turn.t)
      end
    end
  else
    car.pos = car.pos + car.dir * step
    -- Past the crossing it planned for: plan the next one.
    if car.decided_for and vec3.dot(car.pos - car.decided_for, car.dir) > 1.0 then
      car.decided_for = nil
    end
  end
  car.pos.y = RIDE_HEIGHT

  local heading = car.turn and car.turn.t >= 0.0
      and bezier_tangent(car.turn.a, car.turn.p, car.turn.b, car.turn.t) or car.dir
  object:set_position(scene.to_scene(car.pos))
  object:set_yaw(yaw_of(heading))
end

function Traffic:update(dt, focus)
  for i = #self.cars, 1, -1 do
    local car = self.cars[i]
    if not car.object:valid() then
      table.remove(self.cars, i)
    elseif vec3.distance(car.object:world_position(), focus) > self.radius * 1.25 then
      car.object:destroy()
      table.remove(self.cars, i)
    else
      self:drive(car, dt)
    end
  end
  self.timer = self.timer - dt
  if #self.cars < self.count and self.timer <= 0.0 then
    self.timer = 0.2
    self:spawn(focus)
  end
end

-- How many cars there are, and how far they have driven in total (metres).
function Traffic:stats()
  local driven = 0.0
  for _, car in ipairs(self.cars) do driven = driven + car.travelled end
  return #self.cars, driven
end

function Traffic:clear()
  for _, car in ipairs(self.cars) do
    if car.object:valid() then car.object:destroy() end
  end
  self.cars = {}
end

return Traffic
