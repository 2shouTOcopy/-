-- ============================================================================
-- Smart Camera GVCP Protocol Dissector for Wireshark
-- Based on GigE Vision GVCP with private extensions
-- Port: 3966 (instead of standard 3956)
-- ============================================================================

-- Protocol definition
local sc_gvcp = Proto("sc_gvcp", "Smart Camera GVCP Protocol")

-- ============================================================================
-- CSV Register Address to Name Mapping
-- ============================================================================
local register_names = {}

-- CSV file path - modify this path according to your system
local csv_file_path = "Hikrobot_Smart_Device_Profile_addr.csv"

local function load_csv_mapping()
    local file = io.open(csv_file_path, "r")
    if not file then
        -- Try relative path from Wireshark plugins directory
        local info = debug.getinfo(1, "S")
        if info and info.source then
            local script_dir = info.source:match("@(.*/)")
            if script_dir then
                file = io.open(script_dir .. csv_file_path, "r")
            end
        end
    end
    
    if not file then
        print("SC_GVCP: Warning - Could not open CSV file: " .. csv_file_path)
        return
    end
    
    -- Skip header line
    file:read("*line")
    
    local count = 0
    for line in file:lines() do
        local name, addr = line:match("([^,]+),([^,]+)")
        if name and addr then
            -- Remove 0x prefix if present and convert to number
            local addr_str = addr:gsub("^0[xX]", "")
            local addr_num = tonumber(addr_str, 16)
            if addr_num then
                register_names[addr_num] = name
                count = count + 1
            end
        end
    end
    
    file:close()
    print("SC_GVCP: Loaded " .. count .. " register mappings from CSV")
end

-- Load CSV on script initialization
load_csv_mapping()

-- Function to get register name from address
local function get_register_name(addr)
    if register_names[addr] then
        return register_names[addr]
    else
        return string.format("0x%08X", addr)
    end
end

-- ============================================================================
-- Command Definitions
-- ============================================================================
local command_names = {
    -- Standard GVCP Commands
    [0x0002] = "DISCOVERY_CMD",
    [0x0003] = "DISCOVERY_ACK",
    [0x0004] = "FORCEIP_CMD",
    [0x0005] = "FORCEIP_ACK",
    [0x0040] = "PACKET_RESEND_CMD",
    [0x0041] = "PACKET_RESEND_ACK",
    [0x0080] = "READREG_CMD",
    [0x0081] = "READREG_ACK",
    [0x0082] = "WRITEREG_CMD",
    [0x0083] = "WRITEREG_ACK",
    [0x0084] = "READMEM_CMD",
    [0x0085] = "READMEM_ACK",
    [0x0086] = "WRITEMEM_CMD",
    [0x0087] = "WRITEMEM_ACK",
    [0x0089] = "PENDING_ACK",
    [0x00c0] = "EVENT_CMD",
    [0x00c1] = "EVENT_ACK",
    [0x00c2] = "EVENTDATA_CMD",
    [0x00c3] = "EVENTDATA_ACK",
    [0x0100] = "ACTION_CMD",
    [0x0101] = "ACTION_ACK",
    
    -- Private Commands
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

-- Status code definitions (from sc_gvcp_internal.h)
local status_names = {
    -- Standard GVCP status codes
    [0x0000] = "SUCCESS",
    [0x0100] = "PACKET_RESEND",
    [0x8001] = "NOT_IMPLEMENTED",
    [0x8002] = "INVALID_PARAMETER",
    [0x8003] = "INVALID_ADDRESS",
    [0x8004] = "WRITE_PROTECT",
    [0x8005] = "BAD_ALIGNMENT",
    [0x8006] = "ACCESS_DENIED",
    [0x8007] = "BUSY",
    [0x8008] = "LOCAL_PROBLEM",
    [0x8009] = "MSG_MISMATCH",
    [0x800A] = "INVALID_PROTOCOL",
    [0x800B] = "NO_MSG",
    [0x800C] = "PACKET_UNAVAILABLE",
    [0x800D] = "DATA_OVERRUN",
    [0x800E] = "INVALID_HEADER",
    [0x800F] = "WRONG_CONFIG",
    [0x8010] = "PACKET_NOT_YET_AVAILABLE",
    [0x8011] = "PACKET_AND_PREV_REMOVED_FROM_MEMORY",
    [0x8012] = "PACKET_REMOVED_FROM_MEMORY",
    [0x8FFF] = "ERROR",
    
    -- Private status codes (from sc_gvcp_internal.h)
    [0x8601] = "PUBKEY_INVALID",
    [0x8602] = "DEVICE_NOT_ACTIVE",
    [0x8603] = "PWD_FMT_INVALID",
    [0x8604] = "PWD_VERIFY_FAILED",
    [0x8605] = "LOCKED_DENIED",
    [0x8606] = "FILE_INVAID",
    [0x8607] = "FILE_DATA_INVALID",
    [0x8608] = "COOKIE_TIMEOUT",
    [0x8609] = "DIGICAP_DECRY_NG",
    [0x860A] = "INITRUN_VERIFY_NG",
    [0x860B] = "PROGRAM_VERIFY_NG",
    [0x860C] = "DIGICAP_VERIFY_NG",
    [0x860D] = "NEED_AUTH",
    [0x860E] = "REDLINE_DATA_RW_FAILED",
    [0x860F] = "REDLINE_API_FAILED",
    [0x8610] = "NETENV_INVALID",
    [0x8611] = "DEVICE_ALREADY_ACTIVE",
    [0x8612] = "BSP_SECURE_FAILED",
    [0x8613] = "WIRELESS_NOT_CONNECT",
    [0x8614] = "USERNAME_INVALID",

}

-- Safe action flags
local safe_action_flags = {
    [0x01] = "ENCRYPT",
    [0x02] = "AUTH",
    [0x04] = "DATA_ENCRYPT",
    [0x08] = "RED_LINE_DEVICE",
}

-- ============================================================================
-- Protocol Fields
-- ============================================================================

-- Header fields
local f_magic = ProtoField.uint8("sc_gvcp.magic", "Magic", base.HEX)
local f_flag = ProtoField.uint8("sc_gvcp.flag", "Flag", base.HEX)
local f_status = ProtoField.uint16("sc_gvcp.status", "Status", base.HEX)
local f_command = ProtoField.uint16("sc_gvcp.command", "Command", base.HEX)
local f_length = ProtoField.uint16("sc_gvcp.length", "Length", base.DEC)
local f_req_id = ProtoField.uint16("sc_gvcp.req_id", "Request ID", base.DEC)

-- Payload fields
local f_address = ProtoField.uint32("sc_gvcp.address", "Address", base.HEX)
local f_value = ProtoField.uint32("sc_gvcp.value", "Value", base.HEX)
local f_count = ProtoField.uint16("sc_gvcp.count", "Count", base.DEC)
local f_data = ProtoField.bytes("sc_gvcp.data", "Data")

-- Discovery ACK fields
local f_spec_ver_major = ProtoField.uint16("sc_gvcp.spec_ver_major", "Spec Version Major", base.DEC)
local f_spec_ver_minor = ProtoField.uint16("sc_gvcp.spec_ver_minor", "Spec Version Minor", base.DEC)
local f_device_mode = ProtoField.uint32("sc_gvcp.device_mode", "Device Mode", base.HEX)
local f_sale_area = ProtoField.uint16("sc_gvcp.sale_area", "Sale Area", base.DEC)
local f_mac_high = ProtoField.uint16("sc_gvcp.mac_high", "MAC High", base.HEX)
local f_mac_low = ProtoField.uint32("sc_gvcp.mac_low", "MAC Low", base.HEX)
local f_ip_cfg_options = ProtoField.uint32("sc_gvcp.ip_cfg_options", "IP Config Options", base.HEX)
local f_ip_cfg_current = ProtoField.uint32("sc_gvcp.ip_cfg_current", "IP Config Current", base.HEX)

-- Security fields (private extension)
local f_safe_major_ver = ProtoField.uint8("sc_gvcp.safe_major_ver", "Safe Major Version", base.DEC)
local f_safe_minor_ver = ProtoField.uint8("sc_gvcp.safe_minor_ver", "Safe Minor Version", base.DEC)
local f_safe_action = ProtoField.uint8("sc_gvcp.safe_action", "Safe Action", base.HEX)
local f_active = ProtoField.uint8("sc_gvcp.active", "Active State", base.DEC)
local f_lock = ProtoField.uint8("sc_gvcp.lock", "Lock State", base.DEC)
local f_locktime = ProtoField.uint16("sc_gvcp.locktime", "Lock Time (s)", base.DEC)

-- Network fields
local f_current_ip = ProtoField.ipv4("sc_gvcp.current_ip", "Current IP")
local f_current_netmask = ProtoField.ipv4("sc_gvcp.current_netmask", "Current Netmask")
local f_default_gw = ProtoField.ipv4("sc_gvcp.default_gw", "Default Gateway")

-- String fields
local f_manufacturer = ProtoField.string("sc_gvcp.manufacturer", "Manufacturer")
local f_model = ProtoField.string("sc_gvcp.model", "Model Name")
local f_device_ver = ProtoField.string("sc_gvcp.device_ver", "Device Version")
local f_serial = ProtoField.string("sc_gvcp.serial", "Serial Number")
local f_user_name = ProtoField.string("sc_gvcp.user_name", "User Defined Name")

-- Register name field (text representation)
local f_reg_name = ProtoField.string("sc_gvcp.reg_name", "Register Name")

-- Pending ACK fields
local f_pending_timeout = ProtoField.uint16("sc_gvcp.pending_timeout", "Pending Timeout (ms)", base.DEC)

-- Packet resend fields
local f_stream_channel = ProtoField.uint16("sc_gvcp.stream_channel", "Stream Channel", base.DEC)
local f_block_id = ProtoField.uint16("sc_gvcp.block_id", "Block ID", base.DEC)
local f_first_packet = ProtoField.uint32("sc_gvcp.first_packet", "First Packet ID", base.DEC)
local f_last_packet = ProtoField.uint32("sc_gvcp.last_packet", "Last Packet ID", base.DEC)

-- Add all fields to protocol
sc_gvcp.fields = {
    f_magic, f_flag, f_status, f_command, f_length, f_req_id,
    f_address, f_value, f_count, f_data,
    f_spec_ver_major, f_spec_ver_minor, f_device_mode, f_sale_area,
    f_mac_high, f_mac_low, f_ip_cfg_options, f_ip_cfg_current,
    f_safe_major_ver, f_safe_minor_ver, f_safe_action, f_active, f_lock, f_locktime,
    f_current_ip, f_current_netmask, f_default_gw,
    f_manufacturer, f_model, f_device_ver, f_serial, f_user_name,
    f_reg_name, f_pending_timeout,
    f_stream_channel, f_block_id, f_first_packet, f_last_packet
}

-- ============================================================================
-- Helper Functions
-- ============================================================================

local function is_command(cmd)
    -- Commands have even numbers, ACKs have odd numbers
    return (cmd % 2) == 0
end

local function get_command_name(cmd)
    return command_names[cmd] or string.format("UNKNOWN_0x%04X", cmd)
end

local function get_status_name(status)
    return status_names[status] or nil
end

local function get_status_display(status)
    local name = status_names[status]
    if name then
        return name
    else
        return string.format("Unknown status (0x%04X)", status)
    end
end

local function decode_safe_action(action)
    local flags = {}
    for bit, name in pairs(safe_action_flags) do
        if bit32.band(action, bit) ~= 0 then
            table.insert(flags, name)
        end
    end
    if #flags == 0 then
        return "NONE"
    end
    return table.concat(flags, "|")
end

local function trim_null(str)
    return str:gsub("%z+$", "")
end

-- ============================================================================
-- Dissector Functions
-- ============================================================================

local function dissect_discovery_ack(buffer, pinfo, tree, offset)
    local payload_tree = tree:add(sc_gvcp, buffer(offset), "Discovery ACK Payload")
    
    payload_tree:add(f_spec_ver_major, buffer(offset, 2))
    payload_tree:add(f_spec_ver_minor, buffer(offset + 2, 2))
    payload_tree:add(f_device_mode, buffer(offset + 4, 4))
    payload_tree:add(f_sale_area, buffer(offset + 8, 2))
    payload_tree:add(f_mac_high, buffer(offset + 10, 2))
    payload_tree:add(f_mac_low, buffer(offset + 12, 4))
    payload_tree:add(f_ip_cfg_options, buffer(offset + 16, 4))
    payload_tree:add(f_ip_cfg_current, buffer(offset + 20, 4))
    
    -- Security fields (offsets from structure in sc_gvcp_internal.h)
    local safe_major = buffer(offset + 24, 1):uint()
    local safe_minor = buffer(offset + 25, 1):uint()
    local safe_action = buffer(offset + 26, 1):uint()
    local active = buffer(offset + 28, 1):uint()
    local lock = buffer(offset + 29, 1):uint()
    local locktime = buffer(offset + 30, 2):uint()
    
    local sec_tree = payload_tree:add(sc_gvcp, buffer(offset + 24, 8), "Security Info")
    sec_tree:add(f_safe_major_ver, buffer(offset + 24, 1)):append_text(string.format(" (SE V%d)", safe_major))
    sec_tree:add(f_safe_minor_ver, buffer(offset + 25, 1))
    sec_tree:add(f_safe_action, buffer(offset + 26, 1)):append_text(" (" .. decode_safe_action(safe_action) .. ")")
    
    local active_str = active == 0 and "Not Active" or (active == 1 and "Active" or "Abnormal")
    sec_tree:add(f_active, buffer(offset + 28, 1)):append_text(" (" .. active_str .. ")")
    
    local lock_str = lock == 0 and "Unlocked" or "Locked"
    sec_tree:add(f_lock, buffer(offset + 29, 1)):append_text(" (" .. lock_str .. ")")
    sec_tree:add(f_locktime, buffer(offset + 30, 2))
    
    -- IP addresses
    payload_tree:add(f_current_ip, buffer(offset + 36, 4))
    payload_tree:add(f_current_netmask, buffer(offset + 52, 4))
    payload_tree:add(f_default_gw, buffer(offset + 68, 4))
    
    -- String fields
    local man_name = trim_null(buffer(offset + 72, 32):string())
    local model_name = trim_null(buffer(offset + 104, 32):string())
    local device_ver = trim_null(buffer(offset + 136, 32):string())
    local serial = trim_null(buffer(offset + 216, 16):string())
    local user_name = trim_null(buffer(offset + 232, 16):string())
    
    payload_tree:add(f_manufacturer, buffer(offset + 72, 32)):set_text("Manufacturer: " .. man_name)
    payload_tree:add(f_model, buffer(offset + 104, 32)):set_text("Model Name: " .. model_name)
    payload_tree:add(f_device_ver, buffer(offset + 136, 32)):set_text("Device Version: " .. device_ver)
    payload_tree:add(f_serial, buffer(offset + 216, 16)):set_text("Serial Number: " .. serial)
    payload_tree:add(f_user_name, buffer(offset + 232, 16)):set_text("User Name: " .. user_name)
    
    return string.format("[Model=%s, SN=%s, Active=%s]", model_name, serial, active_str)
end

local function dissect_readreg_cmd(buffer, pinfo, tree, offset, length)
    local reg_count = length / 4
    local regs = {}
    
    for i = 0, reg_count - 1 do
        local addr = buffer(offset + i * 4, 4):uint()
        local name = get_register_name(addr)
        tree:add(f_address, buffer(offset + i * 4, 4)):append_text(" (" .. name .. ")")
        table.insert(regs, name)
    end
    
    return "[" .. table.concat(regs, ", ") .. "]"
end

local function dissect_readreg_ack(buffer, pinfo, tree, offset, length)
    local reg_count = length / 4
    local values = {}
    
    for i = 0, reg_count - 1 do
        local value = buffer(offset + i * 4, 4):uint()
        tree:add(f_value, buffer(offset + i * 4, 4))
        table.insert(values, string.format("0x%08X", value))
    end
    
    return "(" .. table.concat(values, ", ") .. ")"
end

local function dissect_writereg_cmd(buffer, pinfo, tree, offset, length)
    local reg_count = length / 8
    local regs = {}
    
    for i = 0, reg_count - 1 do
        local addr = buffer(offset + i * 8, 4):uint()
        local value = buffer(offset + i * 8 + 4, 4):uint()
        local name = get_register_name(addr)
        
        local reg_tree = tree:add(sc_gvcp, buffer(offset + i * 8, 8), string.format("Register %d", i + 1))
        reg_tree:add(f_address, buffer(offset + i * 8, 4)):append_text(" (" .. name .. ")")
        reg_tree:add(f_value, buffer(offset + i * 8 + 4, 4))
        
        table.insert(regs, name)
    end
    
    return "[" .. table.concat(regs, ", ") .. "]"
end

local function dissect_writereg_ack(buffer, pinfo, tree, offset, length)
    if length >= 4 then
        local index = buffer(offset + 2, 2):uint()
        tree:add(f_count, buffer(offset + 2, 2)):set_text("Registers Written: " .. index)
        return nil  -- Status will be shown separately
    end
    return nil
end

local function dissect_readmem_cmd(buffer, pinfo, tree, offset)
    local addr = buffer(offset, 4):uint()
    local size = bit32.band(buffer(offset + 4, 4):uint(), 0xFFFF)
    local name = get_register_name(addr)
    
    tree:add(f_address, buffer(offset, 4)):append_text(" (" .. name .. ")")
    tree:add(f_count, buffer(offset + 4, 4)):set_text("Size: " .. size .. " bytes")
    
    return "[" .. name .. "]"
end

local function dissect_readmem_ack(buffer, pinfo, tree, offset, length)
    if length >= 4 then
        local addr = buffer(offset, 4):uint()
        local name = get_register_name(addr)
        tree:add(f_address, buffer(offset, 4)):append_text(" (" .. name .. ")")
        
        if length > 4 then
            tree:add(f_data, buffer(offset + 4, length - 4))
        end
        
        return "[" .. name .. "]"
    end
    return nil
end

local function dissect_writemem_cmd(buffer, pinfo, tree, offset, length)
    local addr = buffer(offset, 4):uint()
    local name = get_register_name(addr)
    
    tree:add(f_address, buffer(offset, 4)):append_text(" (" .. name .. ")")
    
    if length > 4 then
        tree:add(f_data, buffer(offset + 4, length - 4))
    end
    
    return "[" .. name .. "]"
end

local function dissect_writemem_ack(buffer, pinfo, tree, offset, length, addr_from_cmd)
    if length >= 4 then
        local addr = buffer(offset, 4):uint()
        local name = get_register_name(addr)
        tree:add(f_address, buffer(offset, 4)):append_text(" (" .. name .. ")")
        return "[" .. name .. "]"
    end
    return nil
end

local function dissect_pending_ack(buffer, pinfo, tree, offset)
    local timeout = buffer(offset + 2, 2):uint()
    tree:add(f_pending_timeout, buffer(offset + 2, 2))
    return string.format("(timeout=%dms)", timeout)
end

local function dissect_packet_resend_cmd(buffer, pinfo, tree, offset)
    local ch_id = buffer(offset, 2):uint()
    local block_id = buffer(offset + 2, 2):uint()
    local first_pkt = bit32.band(buffer(offset + 4, 4):uint(), 0x0FFF)
    local last_pkt = bit32.band(buffer(offset + 8, 4):uint(), 0x0FFF)
    
    tree:add(f_stream_channel, buffer(offset, 2))
    tree:add(f_block_id, buffer(offset + 2, 2))
    tree:add(f_first_packet, buffer(offset + 4, 4)):set_text("First Packet ID: " .. first_pkt)
    tree:add(f_last_packet, buffer(offset + 8, 4)):set_text("Last Packet ID: " .. last_pkt)
    
    return string.format("[Ch=%d, Block=%d, Pkts=%d-%d]", ch_id, block_id, first_pkt, last_pkt)
end

local function dissect_forceip_cmd(buffer, pinfo, tree, offset, length)
    if length >= 56 then
        -- MAC address at offset 2-8
        local mac_tree = tree:add(sc_gvcp, buffer(offset + 2, 6), "Target MAC")
        
        -- IP at offset 20
        tree:add(f_current_ip, buffer(offset + 20, 4))
        -- Netmask at offset 36
        tree:add(f_current_netmask, buffer(offset + 36, 4))
        -- Gateway at offset 52
        tree:add(f_default_gw, buffer(offset + 52, 4))
        
        local ip = buffer(offset + 20, 4):ipv4()
        return string.format("[IP=%s]", tostring(ip))
    end
    return nil
end

-- ============================================================================
-- Main Dissector Function
-- ============================================================================

function sc_gvcp.dissector(buffer, pinfo, tree)
    local length = buffer:len()
    if length < 8 then return end
    
    pinfo.cols.protocol = sc_gvcp.name
    
    local subtree = tree:add(sc_gvcp, buffer(), "Smart Camera GVCP Protocol")
    
    -- Parse header
    local byte0 = buffer(0, 1):uint()
    local byte1 = buffer(1, 1):uint()
    local command = buffer(2, 2):uint()
    local payload_len = buffer(4, 2):uint()
    local req_id = buffer(6, 2):uint()
    
    local is_cmd = is_command(command)
    local cmd_name = get_command_name(command)
    
    -- Header tree
    local header_tree = subtree:add(sc_gvcp, buffer(0, 8), "Header")
    
    -- Direction indicator: > for CMD (request), < for ACK (response)
    local direction = is_cmd and "> " or "< "
    local status = 0
    local status_str = nil
    
    if is_cmd then
        -- Command packet
        header_tree:add(f_magic, buffer(0, 1))
        header_tree:add(f_flag, buffer(1, 1))
    else
        -- ACK packet
        status = buffer(0, 2):uint()
        local status_display = get_status_display(status)
        header_tree:add(f_status, buffer(0, 2)):append_text(" (" .. status_display .. ")")
        
        if status == 0 then
            status_str = "(Success)"
        else
            status_str = get_status_display(status)
        end
    end
    
    header_tree:add(f_command, buffer(2, 2)):append_text(" (" .. cmd_name .. ")")
    header_tree:add(f_length, buffer(4, 2))
    header_tree:add(f_req_id, buffer(6, 2))
    
    -- Build Info string
    local info_str = direction .. cmd_name
    local extra_info = nil
    
    -- Parse payload based on command
    if payload_len > 0 and length >= 8 + payload_len then
        local payload_tree = subtree:add(sc_gvcp, buffer(8, payload_len), "Payload")
        
        if command == 0x0003 then  -- DISCOVERY_ACK
            extra_info = dissect_discovery_ack(buffer, pinfo, payload_tree, 8)
        elseif command == 0x0080 then  -- READREG_CMD
            extra_info = dissect_readreg_cmd(buffer, pinfo, payload_tree, 8, payload_len)
        elseif command == 0x0081 then  -- READREG_ACK
            extra_info = dissect_readreg_ack(buffer, pinfo, payload_tree, 8, payload_len)
        elseif command == 0x0082 then  -- WRITEREG_CMD
            extra_info = dissect_writereg_cmd(buffer, pinfo, payload_tree, 8, payload_len)
        elseif command == 0x0083 then  -- WRITEREG_ACK
            dissect_writereg_ack(buffer, pinfo, payload_tree, 8, payload_len)
            -- For WRITEREG_ACK, show status only
        elseif command == 0x0084 then  -- READMEM_CMD
            extra_info = dissect_readmem_cmd(buffer, pinfo, payload_tree, 8)
        elseif command == 0x0085 then  -- READMEM_ACK
            extra_info = dissect_readmem_ack(buffer, pinfo, payload_tree, 8, payload_len)
        elseif command == 0x0086 then  -- WRITEMEM_CMD
            extra_info = dissect_writemem_cmd(buffer, pinfo, payload_tree, 8, payload_len)
        elseif command == 0x0087 then  -- WRITEMEM_ACK
            extra_info = dissect_writemem_ack(buffer, pinfo, payload_tree, 8, payload_len)
        elseif command == 0x0089 then  -- PENDING_ACK
            extra_info = dissect_pending_ack(buffer, pinfo, payload_tree, 8)
        elseif command == 0x0004 then  -- FORCEIP_CMD
            extra_info = dissect_forceip_cmd(buffer, pinfo, payload_tree, 8, payload_len)
        elseif command == 0x0040 then  -- PACKET_RESEND_CMD
            extra_info = dissect_packet_resend_cmd(buffer, pinfo, payload_tree, 8)
        else
            -- Generic payload display
            payload_tree:add(f_data, buffer(8, payload_len))
        end
    end
    
    -- Format Info column based on command type
    if not is_cmd then
        -- ACK packets: show status
        if command == 0x0083 then  -- WRITEREG_ACK
            if status == 0 then
                info_str = info_str .. " (Success)"
            else
                info_str = info_str .. " " .. status_str
            end
        elseif command == 0x0087 then  -- WRITEMEM_ACK
            if status == 0 then
                if extra_info then
                    info_str = info_str .. " " .. extra_info
                end
            else
                info_str = info_str .. " " .. status_str
                if extra_info then
                    info_str = info_str .. " " .. extra_info
                end
            end
        elseif command == 0x0081 then  -- READREG_ACK
            if extra_info then
                info_str = info_str .. " " .. extra_info
            end
        elseif command == 0x0085 then  -- READMEM_ACK
            if extra_info then
                info_str = info_str .. " " .. extra_info
            end
        else
            -- Other ACKs
            if extra_info then
                info_str = info_str .. " " .. extra_info
            end
        end
    else
        -- CMD packets: show register info
        if extra_info then
            info_str = info_str .. " " .. extra_info
        end
    end
    
    pinfo.cols.info = info_str
end

-- ============================================================================
-- Register Protocol
-- ============================================================================

-- Register for UDP port 3966
local udp_table = DissectorTable.get("udp.port")
udp_table:add(3966, sc_gvcp)

print("SC_GVCP: Dissector loaded for port 3966")
