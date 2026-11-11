-- Pedestrians around the player (ROADMAP M6, docs/design/0014).
--
-- Keeps up to `count` people within `radius` metres of the focus: each is a mannequin
-- with an animation graph and a navigation agent that wanders the navigation mesh, so
-- they walk the streets and squares and avoid buildings and each other. People who end
-- up far away are removed and new ones appear out of sight.

local Crowd = {}
Crowd.__index = Crowd

local MODELS = { "models/pedestrian_a.gltf", "models/pedestrian_b.gltf", "models/pedestrian_c.gltf" }

function Crowd.new(count, radius)
  return setmetatable({
    count = count or 24,
    radius = radius or 70.0,
    people = {},
    timer = 0.0,
    spawned = 0,
  }, Crowd)
end

function Crowd:spawn(focus)
  local point = nav.random_point(focus, self.radius)
  if not point or vec3.distance(point, focus) < 14.0 then
    return
  end
  self.spawned = self.spawned + 1
  local person = scene.spawn(MODELS[(self.spawned % #MODELS) + 1], point, math.random() * 360.0)
  person:set_name("Pedestrian" .. self.spawned)
  local clips = person:get("AnimationComponent")
  person:add("AnimatorComponent", { animation = clips.animation, graph = "animations/pedestrian.animgraph" })
  person:add("NavAgentComponent", {
    wanderRadius = 30.0,
    maxSpeed = 1.15 + math.random() * 0.45,
    radius = 0.35,
  })
  table.insert(self.people, { object = person, start = person:world_coords() })
end

function Crowd:update(dt, focus)
  for i = #self.people, 1, -1 do
    local person = self.people[i].object
    if not person:valid() or vec3.distance(person:world_position(), focus) > self.radius * 1.6 then
      if person:valid() then person:destroy() end
      table.remove(self.people, i)
    end
  end
  if not nav.ready() then
    return
  end
  self.timer = self.timer - dt
  if #self.people < self.count and self.timer <= 0.0 then
    self.timer = 0.1
    self:spawn(focus)
  end
end

-- How many people there are, and how far they have walked in total (metres).
function Crowd:stats()
  local walked = 0.0
  for _, entry in ipairs(self.people) do
    if entry.object:valid() then
      walked = walked + vec3.distance(entry.object:world_coords(), entry.start)
    end
  end
  return #self.people, walked
end

function Crowd:clear()
  for _, entry in ipairs(self.people) do
    if entry.object:valid() then entry.object:destroy() end
  end
  self.people = {}
end

return Crowd
