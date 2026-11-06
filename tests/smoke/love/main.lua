-- Prints "R3I6NOS_SMOKE ok ..." and exits 0 when the runtime works;
-- prints "R3I6NOS_SMOKE FAIL: <why>" and exits 1 otherwise.
-- R3I6NOS_EXPECT_VERSION (for example "11.5") must match LÖVE's major.minor.

local function check()
  local major, minor, revision = love.getVersion()
  local expect = os.getenv("R3I6NOS_EXPECT_VERSION")
  local got = major .. "." .. minor
  assert(not expect or expect == got, ("version %s, expected %s"):format(got, expect))

  assert(love.system.getOS() == "Linux", "getOS: " .. tostring(love.system.getOS()))

  -- love.data: hash and zlib round trip.
  -- 12.0 deprecates hash(algorithm, data) for hash(container, algorithm, data); try the new form first.
  local okHash, digest = pcall(love.data.hash, "string", "md5", "abc")
  if not okHash then digest = love.data.hash("md5", "abc") end
  local md5 = love.data.encode("string", "hex", digest)
  assert(md5 == "900150983cd24fb0d6963f7d28e17f72", "md5: " .. md5)
  local packed = love.data.compress("string", "zlib", string.rep("r3i6nos", 100))
  assert(tostring(love.data.decompress("string", "zlib", packed)) == string.rep("r3i6nos", 100), "zlib round trip")

  -- love.filesystem: write and read back in the save directory.
  assert(love.filesystem.write("smoke.txt", "ok"), "filesystem write")
  assert(love.filesystem.read("smoke.txt") == "ok", "filesystem read")
  love.filesystem.remove("smoke.txt")

  -- love.image: make an image and encode it as PNG (lodepng/zlib).
  local data = love.image.newImageData(4, 4)
  data:setPixel(1, 1, 1, 0, 0, 1)
  local png = data:encode("png")
  assert(png:getSize() > 8, "png encode")

  -- love.math, love.physics (Box2D), love.sound (decoders are linked in).
  -- love.math.noise became perlinNoise in 12.0.
  local noise = love.math.perlinNoise or love.math.noise
  assert(noise(0.5) ~= nil, "noise")
  local world = love.physics.newWorld(0, 9.8)
  world:update(1 / 60)
  assert(love.sound.newSoundData(441, 44100, 16, 1):getSampleCount() == 441, "sounddata")

  return ("love=%s.%s.%s jit=%s"):format(major, minor, revision, jit and jit.version or "none")
end

function love.load()
  local ok, result = pcall(check)
  if ok then
    print("R3I6NOS_SMOKE ok " .. result)
    love.event.quit(0)
  else
    print("R3I6NOS_SMOKE FAIL: " .. tostring(result))
    love.event.quit(1)
  end
end
