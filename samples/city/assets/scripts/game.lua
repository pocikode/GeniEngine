-- Geni City: the reference game's rules (ROADMAP M6).
--
-- Attached to the "Game" object by `city --play`. Walk around the streamed city, get in
-- the parked car, drive, get out; pedestrians walk the streets and traffic drives them.
-- A title menu, a pause menu with volume settings and a HUD are drawn with the game UI
-- (docs/design/0015). Everything here is Lua (docs/design/0013): edit it while the game
-- runs and it reloads.
--
-- `game_options` (set by the program) can hold: autopilot (bool), crowd (bool),
-- traffic (bool), screenshot (path), title_only (bool).

local Crowd = require("crowd")
local Traffic = require("traffic")
local Autopilot = require("autopilot")

local Game = {}

-- World coordinates: a mid-rise street 800 m east of the centre (streets run every
-- 32 m; this one is x = 800).
local PLAYER_START = vec3(798.0, 0.05, 827.0)
local CAR_START = vec3(802.4, 0.9, 820.0)
local ENTER_DISTANCE = 3.2
local START_HOUR = 16.0 -- the game starts in the late afternoon
local DAY_MINUTES = 24.0 -- real minutes per game day
-- An evening shower (docs/design/0020), in game hours: clouds build first, then rain.
local CLOUDS_FROM, CLOUDS_UNTIL = 16.75, 18.75
local RAIN_FROM, RAIN_UNTIL = 17.0, 18.5

local function flat_distance(a, b)
  return vec3.distance(vec3(a.x, 0, a.z), vec3(b.x, 0, b.z))
end

function Game:start()
  self.options = game_options or {}
  if self.options.crowd == nil then self.options.crowd = true end
  if self.options.traffic == nil then self.options.traffic = true end
  math.randomseed(20291001)

  self.time = 0.0
  self.state = "title"
  self.menu_page = "main"
  self.driving = false
  self.show_help = true
  self.message = nil
  self.message_time = 0.0
  self.volumes = { master = 0.9, music = 0.55, effects = 1.0 }
  audio.set_volume("master", self.volumes.master)
  audio.set_volume("music", self.volumes.music)
  audio.set_volume("effects", self.volumes.effects)

  -- The streamed world's camera becomes a third-person camera.
  local camera = scene.main_camera()
  camera:remove("FlyCameraComponent")
  camera:get("CameraComponent").near = 0.15
  self.camera = camera:add("ThirdPersonCameraComponent", { distance = 4.6, height = 1.55, pitch = 14.0 })

  -- The player: the mannequin with an animation graph and a character controller.
  self.player = scene.spawn("models/player.gltf", scene.to_scene(PLAYER_START), 0.0)
  self.player:set_name("Player")
  local clips = self.player:get("AnimationComponent")
  self.animator = self.player:add("AnimatorComponent", {
    animation = clips.animation, graph = "animations/character.animgraph" })
  self.character = self.player:add("CharacterControllerComponent", {
    playerInput = true, walkSpeed = 1.6, runSpeed = 5.0, jumpSpeed = 5.2 })

  -- The car, parked on the right-hand lane, and its engine sound.
  self.car = scene.spawn("models/car_red.gltf", scene.to_scene(CAR_START), 0.0)
  self.car:set_name("PlayerCar")
  self.vehicle = self.car:add("VehicleComponent", {})
  self.vehicle:set_input(0.0, 0.0, 0.0, 1.0) -- parked: handbrake on
  self.engine = self.car:add("AudioSourceComponent", {
    clip = "sounds/engine_loop.wav", loop = true, spatial = true, volume = 0.0, autoplay = true,
    minDistance = 4.0, maxDistance = 90.0 })

  self.camera:set_target(self.player)
  self.camera:snap()

  -- The day runs while playing (docs/design/0018). pathYaw 180 sends the afternoon sun
  -- down the north-south streets, so the start is not in a building's shadow.
  local sun = scene.find("Sun")
  self.clock = sun and sun:add("TimeOfDayComponent", { time = START_HOUR, dayLengthMinutes = 0.0, pathYaw = 180.0 })
  self.weather = scene.create("Weather"):add("WeatherComponent", { transitionSeconds = 25.0 })
  -- The block's street lights are on all the time in the viewer (it has no clock); in the
  -- game they come on at dusk.
  local city = scene.find("City")
  for _, child in ipairs(city and city:children() or {}) do
    local light = child:name():find("^StreetLight") and child:get("LightComponent")
    if light then
      light.nightOnly = true
    end
  end

  self.crowd = Crowd.new(self.options.crowd and 24 or 0, 70.0)
  self.traffic = Traffic.new(self.options.traffic and 10 or 0, 140.0)

  audio.music("sounds/music.wav", 2.0, 0.8)
  self.ambience = audio.play("sounds/ambience.wav", { loop = true, volume = 0.3 })

  if self.options.autopilot then
    -- Keep the test drive's street clear of crossing traffic spawning on it.
    self.traffic.avoid_x = 800.0
    self.autopilot = Autopilot.new(self)
    self:set_state("playing")
  else
    self:set_state("title")
  end
end

-- Input groups and the cursor follow the state: menus get the ui actions and a free
-- cursor, play gets the gameplay actions and mouse look.
function Game:set_state(state)
  self.state = state
  local playing = state == "playing"
  input.set_group("gameplay", playing)
  input.set_group("ui", not playing)
  input.capture_cursor(playing and not self.options.autopilot)
  game.set_paused(state == "paused" or state == "settings_pause")
  if self.clock then
    self.clock.dayLengthMinutes = playing and DAY_MINUTES or 0.0
  end
  self.camera.playerInput = playing
end

function Game:focus()
  return self.driving and self.car:world_position() or self.player:world_position()
end

function Game:say(text, seconds)
  self.message = text
  self.message_time = seconds or 2.5
end

function Game:ui_sound(name)
  audio.play("sounds/" .. name .. ".wav", { volume = 0.5 })
end

-- Getting in and out ----------------------------------------------------------------

function Game:near_car()
  return not self.driving and flat_distance(self.player:world_position(), self.car:world_position()) < ENTER_DISTANCE
end

function Game:enter_car()
  self.driving = true
  self.character:set_enabled(false)
  self.player:set_active(false)
  self.vehicle.playerInput = true
  self.camera:set_target(self.car)
  self.camera.distance = 7.5
  self.camera.height = 1.3
  self.camera:snap()
  audio.play("sounds/door.wav", { position = self.car:world_position(), volume = 0.9 })
end

-- A free spot beside the car, driver's side first.
function Game:exit_spot()
  local centre = self.car:world_position()
  for _, side in ipairs({ -1.0, 1.0 }) do
    local spot = centre + self.car:right() * (2.1 * side)
    local hit = physics.spherecast(centre + vec3(0, 0.4, 0), 0.45, self.car:right() * side, 2.1, "static")
    if not hit then
      local ground = physics.raycast(spot + vec3(0, 2.0, 0), vec3(0, -1, 0), 6.0, "static")
      if ground then
        return ground.position + vec3(0, 0.05, 0)
      end
    end
  end
  return nil
end

function Game:exit_car()
  local spot = self:exit_spot()
  if not spot then
    self:say("No room to get out here")
    return
  end
  self.driving = false
  self.vehicle.playerInput = false
  self.vehicle:set_input(0.0, 0.0, 0.0, 1.0)
  self.player:set_active(true)
  self.character:set_enabled(true)
  self.character:teleport(spot)
  self.player:set_yaw(self.car:yaw())
  self.camera:set_target(self.player)
  self.camera.distance = 4.6
  self.camera.height = 1.55
  audio.play("sounds/door.wav", { position = self.car:world_position(), volume = 0.9 })
end

-- Per frame ---------------------------------------------------------------------------

function Game:update(dt)
  self.time = self.time + dt
  if self.message_time > 0.0 then self.message_time = self.message_time - dt end

  if self.state == "title" then
    self:title_screen(dt)
  elseif self.state == "playing" then
    self:play(dt)
    self:update_weather()
  else
    self:pause_screen()
  end

  if self.state ~= "paused" and self.state ~= "settings_pause" then
    local focus = self:focus()
    self.crowd:update(dt, focus)
    self.traffic:update(dt, focus)
  end
  self:update_sounds()
end

function Game:play(dt)
  if self.autopilot then
    self.autopilot:update(dt)
  end
  if input.pressed("pause") then
    self:ui_sound("ui_select")
    self:set_state("paused")
    return
  end
  if input.pressed("interact") then
    if self.driving then
      if math.abs(self.vehicle:speed()) < 4.0 then
        self:exit_car()
      else
        self:say("Slow down to get out")
      end
    elseif self:near_car() then
      self:enter_car()
    end
  end
  if self.driving and input.pressed("horn") then
    audio.play("sounds/horn.wav", { position = self.car:world_position() })
  end
  self:hud()
end

-- The weather follows the clock; the component eases between the targets.
function Game:update_weather()
  if not self.clock or not self.weather then return end
  local hour = self.clock.time
  local cloudy = hour >= CLOUDS_FROM and hour < CLOUDS_UNTIL
  local raining = hour >= RAIN_FROM and hour < RAIN_UNTIL
  self.weather.cloudCover = cloudy and 0.85 or 0.0
  self.weather.fog = raining and 0.3 or 0.0
  self.weather.precipitation = raining and 0.8 or 0.0
  self.weather.wind = vec3(1.5, 0.0, 1.0)
end

function Game:update_sounds()
  -- Engine: off when parked, pitch follows the revs.
  local rpm = self.vehicle:rpm()
  local throttle = self.driving and math.abs(input.value("throttle")) or 0.0
  self.engine.pitch = 0.55 + math.clamp(rpm / 6500.0, 0.0, 1.0) * 1.25
  self.engine.volume = self.driving and (0.45 + 0.4 * throttle) or 0.0

  for _, event in ipairs(self.animator:events()) do
    if event == "footstep" and not self.driving and self.character:grounded() then
      audio.play("sounds/footstep.wav", {
        position = self.player:world_position(), volume = 0.5, pitch = 0.9 + math.random() * 0.2 })
    end
  end
end

-- Screens -----------------------------------------------------------------------------

function Game:title_screen(dt)
  -- The camera circles the player while the city lives around them.
  self.camera.yaw = self.camera.yaw + dt * 7.0
  local accent = ui.accent()
  ui.text("GENI CITY", "top", 0, 120, 120, accent)
  ui.text("the GeniEngine v0.1 reference game", "top", 0, 260, 34)
  if self.menu_page == "settings" then
    if self:settings_menu() then self.menu_page = "main" end
  else
    ui.begin_menu("title", "", 460)
    if ui.button("Play") then
      self:ui_sound("ui_select")
      self.camera:snap()
      self:set_state("playing")
      self:say("Find the red car and press " .. self:key("interact"), 5.0)
    end
    if ui.button("Settings") then
      self:ui_sound("ui_select")
      self.menu_page = "settings"
    end
    if ui.button("Quit") then
      game.quit()
    end
    ui.end_menu()
  end
  ui.text("Streamed 4 km world · Jolt physics · Lua · Recast · miniaudio", "bottom", 0, 40, 24, { 0.75, 0.77, 0.8, 1 })
  if self.options.title_only and self.time > 1.0 and not self.shot then
    self.shot = true
  end
end

function Game:pause_screen()
  ui.fade(0.45)
  if self.state == "settings_pause" then
    if self:settings_menu() then self:set_state("paused") end
    return
  end
  if input.pressed("ui_back") or input.pressed("pause") then
    self:ui_sound("ui_select")
    self:set_state("playing")
    return
  end
  ui.begin_menu("pause", "Paused", 520)
  if ui.button("Resume") then
    self:ui_sound("ui_select")
    self:set_state("playing")
  end
  if ui.button("Settings") then
    self:ui_sound("ui_select")
    self:set_state("settings_pause")
  end
  if ui.button("Quit to title") then
    self:ui_sound("ui_select")
    if self.driving then self:exit_car() end
    self.menu_page = "main"
    self:set_state("title")
  end
  if ui.button("Quit game") then
    game.quit()
  end
  ui.end_menu()
end

-- Returns true when the player leaves the settings.
function Game:settings_menu()
  if input.pressed("ui_back") then
    self:ui_sound("ui_select")
    return true
  end
  ui.begin_menu("settings", "Settings", 620)
  local changed
  self.volumes.master, changed = ui.slider("Master volume", self.volumes.master, 0.0, 1.0, 0.1)
  if changed then audio.set_volume("master", self.volumes.master); self:ui_sound("ui_move") end
  self.volumes.music, changed = ui.slider("Music", self.volumes.music, 0.0, 1.0, 0.1)
  if changed then audio.set_volume("music", self.volumes.music) end
  self.volumes.effects, changed = ui.slider("Effects", self.volumes.effects, 0.0, 1.0, 0.1)
  if changed then audio.set_volume("effects", self.volumes.effects); self:ui_sound("ui_move") end
  self.show_help = ui.toggle("Show controls", self.show_help)
  local back = ui.button("Back")
  ui.end_menu()
  if back then self:ui_sound("ui_select") end
  return back
end

-- The name of the key or button bound to an action, for prompts.
function Game:key(action)
  local pad = input.device() == "gamepad"
  local names = {
    interact = pad and "Y" or "E", jump = pad and "A" or "Space", sprint = pad and "B" or "Shift",
    pause = pad and "Start" or "Esc", horn = pad and "R3" or "H", handbrake = pad and "X" or "Space",
  }
  return names[action] or action
end

function Game:hud()
  local dim = { 0.8, 0.82, 0.86, 0.9 }
  ui.text("GeniEngine v0.1 · Geni City", "top_left", 32, 24, 24, dim)
  local people, _ = self.crowd:stats()
  local cars, _ = self.traffic:stats()
  ui.text(string.format("%d pedestrians · %d cars nearby", people, cars), "top_right", 32, 24, 24, dim)
  if self.clock then
    local minutes = math.floor(self.clock.time * 60.0)
    ui.text(string.format("%02d:%02d", minutes // 60, minutes % 60), "top_right", 32, 58, 34)
  end

  if self.driving then
    local kmh = math.abs(self.vehicle:speed()) * 3.6
    ui.panel("bottom_right", 32, 32, 300, 150, nil, 18)
    ui.text(string.format("%3d", math.floor(kmh + 0.5)), "bottom_right", 140, 70, 86)
    ui.text("km/h", "bottom_right", 60, 92, 30, dim)
    local gear = self.vehicle:gear()
    ui.text(gear < 0 and "R" or (gear == 0 and "N" or ("D" .. gear)), "bottom_right", 60, 132, 30, ui.accent())
    ui.bar("bottom_right", 56, 52, 248, 10, math.clamp(self.vehicle:rpm() / 6500.0, 0.0, 1.0))
  elseif self:near_car() then
    ui.panel("bottom", 0, 150, 360, 64, nil, 14)
    ui.text("[" .. self:key("interact") .. "]  Drive", "bottom", 0, 162, 36, ui.accent())
  end

  if self.show_help then
    local lines
    if self.driving then
      lines = { "W/S  accelerate / brake · reverse", "A/D  steer", self:key("handbrake") .. "  handbrake",
        self:key("horn") .. "  horn", self:key("interact") .. "  get out", self:key("pause") .. "  menu" }
      if input.device() == "gamepad" then
        lines = { "RT/LT  accelerate / brake", "Left stick  steer", "X  handbrake", "Y  get out", "Start  menu" }
      end
    else
      lines = { "WASD  move · mouse  look", self:key("sprint") .. "  run · " .. self:key("jump") .. "  jump",
        self:key("interact") .. "  get in a car", self:key("pause") .. "  menu" }
      if input.device() == "gamepad" then
        lines = { "Left stick  move · right stick  look", "B  run · A  jump", "Y  get in a car", "Start  menu" }
      end
    end
    ui.panel("bottom_left", 24, 24, 430, 24 + #lines * 34, nil, 14)
    for i, line in ipairs(lines) do
      ui.text(line, "bottom_left", 44, 24 + (#lines - i) * 34 + 14, 26, dim)
    end
  end

  if self.message and self.message_time > 0.0 then
    local alpha = math.min(1.0, self.message_time)
    ui.text(self.message, "top", 0, 120, 40, { 1, 1, 1, alpha })
  end
end

function Game:destroy()
  input.override()
  input.capture_cursor(false)
  if self.ambience then audio.stop(self.ambience) end
end

return Game
