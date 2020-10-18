-- Parse logs from https://github.com/freedreno/freedreno/
-- test-texturator.c to generate a src/freedreno/fdl/fd6_layout_test.c
-- block.  We figure out the offsets from blits, but there may be some
-- unrelated blits.  So just save all of them until we find the
-- texture state.  This gives us the base address, and the miplevel #0
-- width/height/depth.  Then work backwards from there finding the
-- blits to the same dst buffer and deducing the miplevel from the
-- minified dimensions

local posix = require "posix"

io.write("Analyzing Data...\n")

local r = rnn.init("a630")
local found_tex = 0

local allblits = {}
local nallblits = 0

function get_first_blit(base, width, height)
  local first_blit = nil

  for n = 0,nallblits-1 do
    local blit = allblits[n]
    if blit.base == base and blit.width == width and blit.height == height then
      if not first_blit or blit.addr < first_blit.addr then
        first_blit = blit
      end
    end
  end

  return first_blit
end

function minify(val, lvls)
  val = val >> lvls
  if val < 1 then
    return 1
  end
  return val
end

function printf(fmt, ...)
  return io.write(string.format(fmt, ...))
end

function start_cmdstream(name)
  io.write("Parsing " .. name .. "\n")
  allblits = {}
  nallblits = 0
end

function draw(primtype, nindx)
  local blit = {}

  local type = "???";
  if primtype == "BLIT_OP_SCALE" then
    -- Just in case, filter out anything that isn't starting
    -- at 0,0
    if r.GRAS_2D_DST_TL.X ~= 0 or r.GRAS_2D_DST_TL.Y ~= 0 then
      return
    end

    blit.width   = r.GRAS_2D_DST_BR.X + 1
    blit.height  = r.GRAS_2D_DST_BR.Y + 1
    blit.pitch   = r.RB_2D_DST_PITCH
    blit.addr    = r.RB_2D_DST_LO | (r.RB_2D_DST_HI << 32)
    blit.ubwc_addr = r.RB_2D_DST_FLAGS_LO | (r.RB_2D_DST_FLAGS_HI << 32)
    blit.ubwc_pitch = r.RB_2D_DST_FLAGS_PITCH
    type="blit";
  else
    blit.width   = r.GRAS_SC_WINDOW_SCISSOR_BR.X + 1
    blit.height  = r.GRAS_SC_WINDOW_SCISSOR_BR.Y + 1
    blit.pitch = r.RB_MRT[0].PITCH
    blit.addr = r.RB_MRT[0].BASE_LO | (r.RB_MRT[0].BASE_HI << 32);
    blit.ubwc_addr = r.RB_MRT_FLAG_BUFFER[0].ADDR_LO | (r.RB_MRT_FLAG_BUFFER[0].ADDR_HI << 32)
    blit.ubwc_pitch = r.RB_MRT_FLAG_BUFFER[0].PITCH.PITCH
    type="draw"
  end
  blit.base    = bos.base(blit.addr)
  blit.ubwc_base = bos.base(blit.uwbc_addr)
  blit.endaddr = 0  -- filled in later

  printf("Found %s: 0x%x/%d (0x%x) %dx%d UBWC 0x%x/%d (0x%x)\n",
         type, blit.addr, blit.pitch, blit.base, blit.width, blit.height, blit.ubwc_addr, blit.ubwc_pitch, blit.ubwc_base)

  allblits[nallblits] = blit
  nallblits = nallblits + 1
end

function A6XX_TEX_CONST(pkt, size)
  -- ignore any texture state w/ DEPTH=1, these aren't the 3d tex state we
  -- are looking for

  local base = pkt[4].BASE_LO | (pkt[5].BASE_HI << 32)
  local ubwc_base = pkt[7].FLAG_LO | (pkt[8].FLAG_HI << 32)
  local width0  = pkt[1].WIDTH
  local height0 = pkt[1].HEIGHT
  local depth0  = pkt[5].DEPTH

  if (found_tex ~= 0) then
    return
  end
  found_tex = 1

  printf("Found texture state:\n  %ux%ux%u (%s, %s, MIN_LAYERSZ=0x%x, TILE_ALL=%s, UBWC=%s FLAG_LOG2=%ux%u %s)\n",
         width0, height0, depth0, pkt[0].FMT, pkt[0].TILE_MODE, pkt[3].MIN_LAYERSZ, tostring(pkt[3].TILE_ALL), tostring(pkt[3].FLAG), pkt[10].FLAG_BUFFER_LOGW, pkt[10].FLAG_BUFFER_LOGH, tostring(pkt[0].SAMPLES))

  -- Note that in some case the texture has some extra page or so
  -- at the beginning:
  local basebase = bos.base(base)
  printf("base: 0x%x (0x%x)\n", base, basebase)
  printf("ubwcbase: 0x%x (0x%x)\n", ubwc_base, bos.base(ubwc_base))

  -- see if we can find the associated blits..  The blob always seems to
  -- start from the lower (larger) mipmap levels and layers, so we don't
  -- need to sort by dst address.  Also, while we are at it, fill in the
  -- end-addr (at least for everything but the last blit)
  local blits = {}
  local nblits = 0
  local lastblit = nil
  for n = 0,nallblits-1 do
    local blit = allblits[n]
    --printf("blit addr: 0x%x (0x%x)\n", blit.addr, blit.base)
    if blit.base == basebase and blit.addr >= base then
      blits[nblits] = blit
      nblits = nblits + 1
      if lastblit then
        lastblit.endaddr = blit.addr
      end
      lastblit = blit
    end
  end

  printf("	{\n")
  printf("		.format = %s,\n", pkt[0].FMT)
  if (tostring(pkt[2].TYPE) == "A6XX_TEX_3D") then
    printf("		.is_3d = true,\n")
  end

  printf("		.layout = {\n")
  printf("			.tile_mode = %s,\n", pkt[0].TILE_MODE)
  printf("			.ubwc = %s,\n", tostring(pkt[3].FLAG))

  if (tostring(pkt[0].SAMPLES) == "MSAA_ONE") then
    -- Ignore it, 1 is the default
  elseif (tostring(pkt[0].SAMPLES) == "MSAA_TWO") then
    printf("			.nr_samples = 2,\n")
  elseif (tostring(pkt[0].SAMPLES) == "MSAA_FOUR") then
    printf("			.nr_samples = 4,\n")
  else
    printf("			.nr_samples = XXX,\n")
  end

  if (tostring(pkt[2].TYPE) == "A6XX_TEX_3D") then
    printf("			.width0 = %d, .height0 = %d, .depth = %d,\n", width0, height0, depth0)
  else
    printf("			.width0 = %d, .height0 = %d,\n", width0, height0)
  end

  printf("			.slices = {\n")
  local w = 0
  local h = 0
  local level = 0
  repeat
    local w = minify(width0, level)
    local h = minify(height0, level)
    local blit = get_first_blit(basebase, w, h)
    if blit then
      printf("				{ .offset = %d, .pitch = %u },\n",
          blit.addr - base,
          blit.pitch);
    end
    level = level + 1
  until w == 1 and h == 1
  printf("			},\n")

  if pkt[3].FLAG then
    printf("			.ubwc_slices = {\n")
    level = 0
    repeat
      local w = minify(width0, level)
      local h = minify(height0, level)
      local blit = get_first_blit(basebase, w, h)
      if blit then
        printf("				{ .offset = %d, .pitch = %u },\n",
            blit.ubwc_addr - ubwc_base,
            blit.ubwc_pitch);
      end
      level = level + 1
    until w == 1 and h == 1
    printf("			},\n")
  end

  printf("		},\n")
  printf("	},\n")
  printf("\n\n")
end

