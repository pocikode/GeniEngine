-- The scripted test run of the reference game (`city --play --autopilot`, ROADMAP M6).
--
-- Plays the game through the same input actions a player uses (input.override): walk to
-- the car, get in, drive down the street, brake, get out and walk on. Then it checks
-- that each step happened and that pedestrians and traffic moved, and stores the
-- verdict in the global `autopilot_result` for the program to report.

local Autopilot = {}
Autopilot.__index = Autopilot

function Autopilot.new(game)
  return setmetatable({ game = game, phase = "walk", time = 0.0, total = 0.0, notes = {} }, Autopilot)
end

local function flat(v) return vec3(v.x, 0.0, v.z) end

-- Camera-relative stick values that move the player towards `target`.
local function steer_towards(g, target)
  local camera = scene.main_camera()
  local forward = flat(camera:forward()):normalized()
  local right = flat(camera:right()):normalized()
  local to = flat(target - g.player:world_position())
  local dir = to:normalized()
  return vec3.dot(dir, right), vec3.dot(dir, forward), to:length()
end

function Autopilot:next(phase)
  self.phase = phase
  self.time = 0.0
end

function Autopilot:fail(message)
  input.override()
  autopilot_result = { ok = false, message = message }
  log.error("autopilot: " .. message)
  self.phase = "done"
  game.quit()
end

function Autopilot:update(dt)
  local g = self.game
  self.time = self.time + dt
  self.total = self.total + dt
  if self.phase == "done" then return end
  if self.total > 60.0 then return self:fail("timed out in phase " .. self.phase) end

  if self.phase == "walk" then
    if not self.walk_start then self.walk_start = g.player:world_coords() end
    -- To the driver's door (the car's left side).
    local door = g.car:world_position() - g.car:right() * 1.8
    local x, y, distance = steer_towards(g, door)
    input.override("move_x", x)
    input.override("move_y", y)
    if distance < 1.6 then
      input.override("move_x", 0.0)
      input.override("move_y", 0.0)
      self.walked = vec3.distance(g.player:world_coords(), self.walk_start)
      self:next("enter")
    elseif self.time > 20.0 then
      return self:fail(string.format("did not reach the car (%.1f m away)", distance))
    end
  elseif self.phase == "enter" then
    input.override("interact", self.time < 0.05 and 1.0 or 0.0)
    if g.driving then
      input.override("interact", 0.0)
      self.drive_start = g.car:world_coords()
      self:next("drive")
    elseif self.time > 1.0 then
      return self:fail("pressing interact next to the car did not start driving")
    end
  elseif self.phase == "drive" then
    input.override("throttle", 1.0)
    input.override("steer", 0.0)
    if g.options.screenshot and not self.shot and self.time > 4.0 then
      game.screenshot(g.options.screenshot)
      self.shot = true
    end
    if self.time > 6.0 then
      input.override("throttle", 0.0)
      self.top_speed = g.vehicle:speed()
      self:next("brake")
    end
  elseif self.phase == "brake" then
    input.override("brake", 1.0)
    if math.abs(g.vehicle:speed()) < 0.5 then
      input.override("brake", 0.0)
      self.driven = vec3.distance(g.car:world_coords(), self.drive_start)
      self:next("exit")
    elseif self.time > 8.0 then
      return self:fail("the car did not stop")
    end
  elseif self.phase == "exit" then
    input.override("interact", self.time < 0.05 and 1.0 or 0.0)
    if not g.driving and self.time > 0.2 then
      input.override("interact", 0.0)
      self.exit_at = g.player:world_coords()
      self:next("walk_on")
    elseif self.time > 1.0 then
      return self:fail("pressing interact in the stopped car did not get out")
    end
  elseif self.phase == "walk_on" then
    input.override("move_y", 1.0)
    input.override("sprint", 1.0)
    if self.time > 2.5 then
      input.override()
      self.walked = self.walked + vec3.distance(g.player:world_coords(), self.exit_at)
      self:finish()
    end
  end
end

function Autopilot:finish()
  local g = self.game
  local people, walked = g.crowd:stats()
  local cars, driven = g.traffic:stats()
  local report = string.format(
    "walked %.1f m, drove %.1f m (top speed %.1f km/h), %d pedestrians walked %.0f m, %d cars drove %.0f m",
    self.walked or 0, self.driven or 0, (self.top_speed or 0) * 3.6, people, walked, cars, driven)
  log.info("autopilot: " .. report)
  local problems = {}
  if (self.walked or 0) < 5.0 then table.insert(problems, "the player hardly walked") end
  if (self.driven or 0) < 30.0 then table.insert(problems, "the car hardly moved") end
  if g.options.crowd and (people == 0 or walked < 5.0) then table.insert(problems, "no pedestrians moving") end
  if g.options.traffic and (cars == 0 or driven < 20.0) then table.insert(problems, "no traffic moving") end
  -- The drive is the first mission (docs/design/0024): car, crossing, out.
  if missions.status("test_drive") ~= "completed" then
    table.insert(problems, "the test drive mission did not complete (" .. tostring(missions.status("test_drive")) .. ")")
  end
  autopilot_result = { ok = #problems == 0, message = #problems == 0 and report or table.concat(problems, "; ") .. " (" .. report .. ")" }
  self.phase = "done"
  game.quit()
end

return Autopilot
