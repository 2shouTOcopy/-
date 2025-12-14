-- sc_gvcp.lua
-- Wireshark Lua dissector for "sc_gvcp" (GVCP-like) on UDP port 3966
-- 目标：
-- 1) 解析 GVCP 头部（Command vs Ack）
-- 2) 解析常见命令：Read/Write Register, Read/Write Memory, PendingAck
-- 3) 预留 CSV 映射：寄存器地址 -> 节点名（后续你给数据再补全）

local sc_gvcp = Proto("sc_gvcp", "SC GVCP")

-- ------------------------
-- Preferences (后续可扩展 CSV 路径等)
-- ------------------------
sc_gvcp.prefs.udp_port = Pref.uint("UDP port", 3966, "SC GVCP UDP port")
sc_gvcp.prefs.regmap_csv = Pref.string("Register map CSV (optional)", "", "CSV path for addr->name mapping")

-- ------------------------
-- Fields
-- ------------------------
local f = sc_gvcp.fields

-- Common header fields
f.is_cmd     = ProtoField.bool("sc_gvcp.is_cmd", "Is Command", 8, nil, 0x01)

-- Command header: key_code(1)=0x42, flags(1), command(2), length(2), req_id(2)
f.key_code   = ProtoField.uint8("sc_gvcp.key_code", "Key Code", base.HEX)
f.flags      = ProtoField.uint8("sc_gvcp.flags", "Flags", base.HEX)
f.cmd        = ProtoField.uint16("sc_gvcp.cmd", "Command", base.HEX)
f.length     = ProtoField.uint16("sc_gvcp.length", "Length", base.DEC)
f.req_id     = ProtoField.uint16("sc_gvcp.req_id", "Request ID", base.HEX)

-- Ack header: status(2), acknowledge(2), length(2), req_id(2)
f.status     = ProtoField.uint16("sc_gvcp.status", "Status", base.HEX)
f.ack        = ProtoField.uint16("sc_gvcp.ack", "Acknowledge", base.HEX)

-- Payload fields (subset)
f.reg_addr   = ProtoField.uint32("sc_gvcp.reg.addr", "Register Address", base.HEX)
f.reg_value  = ProtoField.uint32("sc_gvcp.reg.value", "Register Value", base.HEX)

f.mem_addr   = ProtoField.uint32("sc_gvcp.mem.addr", "Memory Address", base.HEX)
f.mem_count  = ProtoField.uint16("sc_gvcp.mem.count", "Count", base.DEC)
f.mem_data   = ProtoField.bytes("sc_gvcp.mem.data", "Data")

f.data_index = ProtoField.uint16("sc_gvcp.data_index", "Data Index", base.DEC)
f.ttc_ms     = ProtoField.uint16("sc_gvcp.pending.time_to_completion_ms", "Time To Completion (ms)", base.DEC)

-- ------------------------
-- Command name map (标准 + 你提供的私有扩展先挂上名字)
-- ------------------------
local CMD = {
  [0x0002] = "DISCOVERY_CMD",
  [0x0003] = "DISCOVERY_ACK",
  [0x0004] = "FORCEIP_CMD",
  [0x0005] = "FORCEIP_ACK",
  [0x0040] = "PACKET_RESEND_CMD",
  [0x0041] = "PACKET_RESEND_ACK",
  [0x0080] = "READ_REGISTER_CMD",
  [0x0081] = "READ_REGISTER_ACK",
  [0x0082] = "WRITE_REGISTER_CMD",
  [0x0083] = "WRITE_REGISTER_ACK",
  [0x0084] = "READ_MEMORY_CMD",
  [0x0085] = "READ_MEMORY_ACK",
  [0x0086] = "WRITE_MEMORY_CMD",
  [0x0087] = "WRITE_MEMORY_ACK",
  [0x0089] = "PENDING_ACK",
  [0x00C0] = "EVENT_CMD",
  [0x00C1] = "EVENT_ACK",
  [0x00C2] = "EVENTDATA_CMD",
  [0x00C3] = "EVENTDATA_ACK",
  [0x0100] = "ACTION_CMD",
  [0x0101] = "ACTION_ACK",

  -- private extensions (先只显示名字，payload 后续再补)
  [0x1000] = "INTERCHANGE_CMD",
  [0x1001] = "INTERCHANGE_ACK",
  [0x1002] = "ACTIVE_CMD",
  [0x1003] = "ACTIVE_ACK",
  [0x1004] = "LOGIN_CMD",
  [0x1005] = "LOGIN_ACK",
  [0x1006] = "CHANGEPWD_CMD",
  [0x1007] = "CHANGEPWD_ACK",
  [0x1008] = "RESETPWD_CMD",
  [0x1009] = "RESETPWD_ACK",
  [0x100A] = "UPGRADE_CMD",
  [0x100B] = "UPGRADE_ACK",
  [0x100C] = "RESTORE_CMD",
  [0x100D] = "RESTORE_ACK",
  [0x100E] = "GETDEVINFO_CMD",
  [0x100F] = "GETDEVINFO_ACK",
  [0x1010] = "GETLOCKINFO_CMD",
  [0x1011] = "GETLOCKINFO_ACK",
  [0x1012] = "GETUSERINFO_CMD",
  [0x1013] = "GETUSERINFO_ACK",
}

local function cmd_name(v)
  return CMD[v] or string.format("UNKNOWN(0x%04X)", v)
end

-- ------------------------
-- Address->Name map (后续从 CSV 填充)
-- ------------------------
local reg_name_map = {} -- [uint32_addr] = "NodeName"

local function addr_to_name(addr_u32)
  local n = reg_name_map[addr_u32]
  if n and n ~= "" then
    return n
  end
  return string.format("0x%08X", addr_u32)
end

local function load_regmap_csv(path)
  reg_name_map = {}
  if not path or path == "" then return end

  local fp = io.open(path, "r")
  if not fp then
    -- 读不到文件就不报错炸 Wireshark，只是禁用映射
    return
  end

  -- 期望格式示例（后续你拿到真实 CSV 我们再对齐）：
  -- Address,Name
  -- 0x00000000,DeviceMode
  for line in fp:lines() do
    -- 跳过空行/注释
    if line and line ~= "" and not line:match("^%s*#") then
      local a, name = line:match("^%s*([^,]+)%s*,%s*(.-)%s*$")
      if a and name then
        local addr = a
        addr = addr:gsub("^0x", ""):gsub("^0X", "")
        local v = tonumber(addr, 16)
        if v then
          reg_name_map[v] = name
        end
      end
    end
  end
  fp:close()
end

-- ------------------------
-- Simple request->response association by (src,dst,req_id)
-- ------------------------
local req_cache = {} -- key -> { readreg_addrs = {..}, writemem_addr = x, ... }

local function key_cmd(pinfo, req_id)
  return tostring(pinfo.src) .. "|" .. tostring(pinfo.dst) .. "|" .. string.format("%04X", req_id)
end
local function key_ack(pinfo, req_id)
  -- ack 方向相反：用 dst|src|req_id 去匹配 cmd
  return tostring(pinfo.dst) .. "|" .. tostring(pinfo.src) .. "|" .. string.format("%04X", req_id)
end

-- ------------------------
-- Payload dissectors (subset)
-- ------------------------
local function dissect_readreg_cmd(tvb, pinfo, subtree, off, payload_len, req_id)
  local n = math.floor(payload_len / 4)
  local addrs = {}
  for i = 0, n - 1 do
    local addr = tvb(off + i * 4, 4):uint()
    table.insert(addrs, addr)

    local label = addr_to_name(addr)
    local it = subtree:add(f.reg_addr, tvb(off + i * 4, 4))
    it:append_text(" (" .. label .. ")")
  end

  req_cache[key_cmd(pinfo, req_id)] = { readreg_addrs = addrs }
end

local function dissect_readreg_ack(tvb, pinfo, subtree, off, payload_len, req_id)
  local n = math.floor(payload_len / 4)
  local ctx = req_cache[key_ack(pinfo, req_id)]
  local addrs = ctx and ctx.readreg_addrs or nil

  for i = 0, n - 1 do
    local vbuf = tvb(off + i * 4, 4)
    local v = vbuf:uint()

    if addrs and addrs[i + 1] then
      local addr = addrs[i + 1]
      local label = addr_to_name(addr)
      local item = subtree:add(f.reg_value, vbuf)
      item:set_text(string.format("%s = 0x%08X", label, v))
    else
      subtree:add(f.reg_value, vbuf)
    end
  end
end

local function dissect_writereg_cmd(tvb, pinfo, subtree, off, payload_len, req_id)
  local n = math.floor(payload_len / 8)
  for i = 0, n - 1 do
    local base = off + i * 8
    local addr = tvb(base, 4):uint()
    local val  = tvb(base + 4, 4):uint()

    local label = addr_to_name(addr)
    local pair_tree = subtree:add(string.format("WRITEREG[%d]: %s", i, label))
    pair_tree:add(f.reg_addr, tvb(base, 4)):append_text(" (" .. label .. ")")
    pair_tree:add(f.reg_value, tvb(base + 4, 4)):append_text(string.format(" (0x%08X)", val))
  end
  -- 这里也可以缓存 addr_list，用于 WRITE_REGISTER_ACK 的“data_index”语义增强（后续补）
  req_cache[key_cmd(pinfo, req_id)] = { }
end

local function dissect_writereg_ack(tvb, subtree, off, payload_len)
  -- Wireshark 原生 dissector 取 data_index 在 payload 的 +2 位置  [oai_citation:1‡sources.debian.org](https://sources.debian.org/src/wireshark/1.12.1%2Bg01b65bf-4%2Bdeb8u14/epan/dissectors/packet-gvcp.c/)
  if payload_len >= 4 then
    subtree:add(f.data_index, tvb(off + 2, 2))
  else
    subtree:add(tvb(off, payload_len), "WRITEREG_ACK payload (short)")
  end
end

local function dissect_readmem_cmd(tvb, subtree, off, payload_len)
  -- readmem_cmd: addr(4) + reserved(2) + count(2 at +6)  [oai_citation:2‡sources.debian.org](https://sources.debian.org/src/wireshark/1.12.1%2Bg01b65bf-4%2Bdeb8u14/epan/dissectors/packet-gvcp.c/)
  if payload_len >= 8 then
    local addr = tvb(off, 4):uint()
    subtree:add(f.mem_addr, tvb(off, 4)):append_text(" (" .. addr_to_name(addr) .. ")")
    subtree:add(f.mem_count, tvb(off + 6, 2))
  else
    subtree:add(tvb(off, payload_len), "READMEM_CMD payload (short)")
  end
end

local function dissect_readmem_ack(tvb, subtree, off, payload_len)
  -- readmem_ack: addr(4) + data(length-4)  [oai_citation:3‡sources.debian.org](https://sources.debian.org/src/wireshark/1.12.1%2Bg01b65bf-4%2Bdeb8u14/epan/dissectors/packet-gvcp.c/)
  if payload_len >= 4 then
    local addr = tvb(off, 4):uint()
    subtree:add(f.mem_addr, tvb(off, 4)):append_text(" (" .. addr_to_name(addr) .. ")")
    if payload_len > 4 then
      subtree:add(f.mem_data, tvb(off + 4, payload_len - 4))
    end
  else
    subtree:add(tvb(off, payload_len), "READMEM_ACK payload (short)")
  end
end

local function dissect_writemem_cmd(tvb, pinfo, subtree, off, payload_len, req_id)
  -- writemem_cmd: addr(4) + data(length-4)  [oai_citation:4‡sources.debian.org](https://sources.debian.org/src/wireshark/1.12.1%2Bg01b65bf-4%2Bdeb8u14/epan/dissectors/packet-gvcp.c/)
  if payload_len >= 4 then
    local addr = tvb(off, 4):uint()
    subtree:add(f.mem_addr, tvb(off, 4)):append_text(" (" .. addr_to_name(addr) .. ")")
    if payload_len > 4 then
      subtree:add(f.mem_data, tvb(off + 4, payload_len - 4))
    end
    req_cache[key_cmd(pinfo, req_id)] = { writemem_addr = addr }
  else
    subtree:add(tvb(off, payload_len), "WRITEMEM_CMD payload (short)")
  end
end

local function dissect_writemem_ack(tvb, subtree, off, payload_len)
  -- writemem_ack: data_index at +2 (2 bytes)  [oai_citation:5‡sources.debian.org](https://sources.debian.org/src/wireshark/1.12.1%2Bg01b65bf-4%2Bdeb8u14/epan/dissectors/packet-gvcp.c/)
  if payload_len >= 4 then
    subtree:add(f.data_index, tvb(off + 2, 2))
  else
    subtree:add(tvb(off, payload_len), "WRITEMEM_ACK payload (short)")
  end
end

local function dissect_pending_ack(tvb, subtree, off, payload_len)
  -- pending_ack: time_to_completion at +2 (2 bytes)  [oai_citation:6‡sources.debian.org](https://sources.debian.org/src/wireshark/1.12.1%2Bg01b65bf-4%2Bdeb8u14/epan/dissectors/packet-gvcp.c/)
  if payload_len >= 4 then
    subtree:add(f.ttc_ms, tvb(off + 2, 2))
  else
    subtree:add(tvb(off, payload_len), "PENDING_ACK payload (short)")
  end
end

-- ------------------------
-- Main dissector
-- ------------------------
function sc_gvcp.dissector(tvb, pinfo, tree)
  local pktlen = tvb:len()
  if pktlen < 8 then return end

  pinfo.cols.protocol = "SC_GVCP"

  local key = tvb(0, 1):uint()
  local is_cmd = (key == 0x42)

  local t = tree:add(sc_gvcp, tvb(), "SC GVCP")
  t:add(f.is_cmd, tvb(0,1)):set_generated(true)

  local hdr = t:add("Header")

  local cmd_or_ack = nil
  local length = 0
  local req_id = 0

  if is_cmd then
    hdr:add(f.key_code, tvb(0,1))
    hdr:add(f.flags, tvb(1,1))
    cmd_or_ack = tvb(2,2):uint()
    length     = tvb(4,2):uint()
    req_id     = tvb(6,2):uint()

    hdr:add(f.cmd, tvb(2,2)):append_text(" (" .. cmd_name(cmd_or_ack) .. ")")
    hdr:add(f.length, tvb(4,2))
    hdr:add(f.req_id, tvb(6,2))

    pinfo.cols.info = cmd_name(cmd_or_ack) .. string.format(" req=0x%04X len=%d", req_id, length)
  else
    -- Ack header: status(2), acknowledge(2), length(2), req_id(2)  [oai_citation:7‡sources.debian.org](https://sources.debian.org/src/wireshark/1.12.1%2Bg01b65bf-4%2Bdeb8u14/epan/dissectors/packet-gvcp.c/)
    local status = tvb(0,2):uint()
    cmd_or_ack   = tvb(2,2):uint()
    length       = tvb(4,2):uint()
    req_id       = tvb(6,2):uint()

    hdr:add(f.status, tvb(0,2))
    hdr:add(f.ack, tvb(2,2)):append_text(" (" .. cmd_name(cmd_or_ack) .. ")")
    hdr:add(f.length, tvb(4,2))
    hdr:add(f.req_id, tvb(6,2))

    pinfo.cols.info = cmd_name(cmd_or_ack) .. string.format(" req=0x%04X len=%d status=0x%04X", req_id, length, status)
  end

  local payload_off = 8
  local available = math.max(0, pktlen - payload_off)
  local payload_len = math.min(length, available)

  if payload_len <= 0 then return end

  local pl = t:add("Payload", tvb(payload_off, payload_len))

  -- Dispatch by command/ack id
  if cmd_or_ack == 0x0080 then
    dissect_readreg_cmd(tvb, pinfo, pl, payload_off, payload_len, req_id)
  elseif cmd_or_ack == 0x0081 then
    dissect_readreg_ack(tvb, pinfo, pl, payload_off, payload_len, req_id)
  elseif cmd_or_ack == 0x0082 then
    dissect_writereg_cmd(tvb, pinfo, pl, payload_off, payload_len, req_id)
  elseif cmd_or_ack == 0x0083 then
    dissect_writereg_ack(tvb, pl, payload_off, payload_len)
  elseif cmd_or_ack == 0x0084 then
    dissect_readmem_cmd(tvb, pl, payload_off, payload_len)
  elseif cmd_or_ack == 0x0085 then
    dissect_readmem_ack(tvb, pl, payload_off, payload_len)
  elseif cmd_or_ack == 0x0086 then
    dissect_writemem_cmd(tvb, pinfo, pl, payload_off, payload_len, req_id)
  elseif cmd_or_ack == 0x0087 then
    dissect_writemem_ack(tvb, pl, payload_off, payload_len)
  elseif cmd_or_ack == 0x0089 then
    dissect_pending_ack(tvb, pl, payload_off, payload_len)
  else
    -- 其他命令先 raw 展示，等你给私有信令结构再逐个补齐
    pl:add(tvb(payload_off, payload_len), "Raw payload (" .. payload_len .. " bytes)")
  end
end

-- ------------------------
-- Register to UDP port
-- ------------------------
local udp_table = DissectorTable.get("udp.port")

function sc_gvcp.init()
  -- reload mapping if prefs changed
  load_regmap_csv(sc_gvcp.prefs.regmap_csv)
end

function sc_gvcp.prefs_changed()
  -- 端口变更：先尽量 remove 老端口再 add 新端口（不同 Wireshark 版本 remove 支持不一致）
  pcall(function() udp_table:remove(sc_gvcp.prefs.udp_port, sc_gvcp) end)
  udp_table:add(sc_gvcp.prefs.udp_port, sc_gvcp)
  load_regmap_csv(sc_gvcp.prefs.regmap_csv)
end

-- initial register
udp_table:add(sc_gvcp.prefs.udp_port, sc_gvcp)