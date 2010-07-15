--------------------------------------------------------------------------------
--
-- eLuaMIDI, a midi protocol API for eLua (www.eluaproject.net)
--
-- v0.3, Feb 2010, by Thiago Naves, LED Lab, PUC-Rio
--
--------------------------------------------------------------------------------
--
-- Available functions:
--   init( port )
--   message_data_size( status )
--   receive( timeout, timer_id )
--   send_control_change( channel, control, value )
--   send_14bit_control_change( channel, control_coarse, control_fine, value )
--   send_note_on( channel, note, velocity )
--   send_note_off( channel, note, velocity )
--   send_after_touch( channel, note, pressure )
--   send_program_change( channel, program )
--   send_channel_pressure( channel, pressure )
--   send_pitch_wheel( channel, pitch )
--   send_system_exclusive( id, data )
--   send_gm_system_enable( channel )
--   send_gm_system_disable( channel )
--   send_master_volume( channel, volume )
--   send_quarter_frame( time_code )
--   send_song_position( beat )
--   send_song_select( song )
--   send_tune_request()
--   send_clock()
--   send_start()
--   send_continue()
--   send_stop()
--   send_active_sense()
--   send_reset()
--   send_all_notes_off( channel )
--   send_all_sound_off( channel )
--   send_all_controllers_off( channel )
--   send_mono_operation( channel )
--   send_poly_operation( channel )

local uart = uart
local assert = assert
local string = string
local print = print
local tonumber = tonumber
local bit = bit

module(...)

-- Configuration and Constants table
defs = {}

-- Define uart configuration
defs[ "baud" ] = 31250
defs[ "data_bits" ] = 8
defs[ "stop_bits" ] = uart.STOP_1
defs[ "parity" ] = uart.PAR_NONE

-- Midi messages
defs[ "note_off" ] = 0x80
defs[ "note_on" ] = 0x90
defs[ "after_touch" ] = 0xA0
defs[ "key_pressure" ] = defs[ "after_touch" ]
defs[ "control_change" ] = 0xB0
defs[ "program_change" ] = 0xC0
defs[ "channel_pressure" ] = 0xD0
defs[ "pitch_wheel" ] = 0xE0
defs[ "system_exclusive_begin" ] = 0xF0
defs[ "system_exclusive_end" ] = 0xF7

defs[ "pitch_wheel_middle" ] = 8192
defs[ "default_note_velocity" ] = 64

-- Midi IDs ( generic system excluseve IDs )
defs[ "realtime_id" ] = 0x7F
defs[ "non_realtime_id" ] = 0x7E
defs[ "educational_id" ] = 0x7D

-- Midi System Exclusive Sub-IDs
defs[ "se_gm_system_enable_disable" ] = 0x09
defs[ "se_device_control" ] = 0x04
defs[ "se_master_volume" ] = 0x01

-- Midi Time Messages
defs[ "quarter_frame" ] = 0xF1
defs[ "song_position" ] = 0xF2
defs[ "song_select" ] = 0xF3
defs[ "tune_request" ] = 0xF6
defs[ "clock" ] = 0xF8
defs[ "start" ] = 0xFA
defs[ "continue" ] = 0xFB
defs[ "stop" ] = 0xFC
defs[ "active_sense" ] = 0xFE
defs[ "reset" ] = 0xFF

-- Message table index
defs[ "msg_code" ] = 1
defs[ "msg_channel" ] = 2
defs[ "msg_data" ] = 3
defs[ "msg_data2" ] = 4

-- Midi Receive return codes
defs[ "msg_new_message" ] = 1
defs[ "msg_in_message" ] = 2
defs[ "msg_no_message" ] = 3
defs[ "msg_size_unknown" ] = -1

-- Midi default controllers ( for especial messages )
defs[ "cc_all_notes_off" ] = 123
defs[ "cc_all_sound_off" ] = 120
defs[ "cc_all_controllers_off" ] = 121
defs[ "cc_mono_operation" ] = 126
defs[ "cc_poly_operation" ] = 127

-- Holds the uart ID to which send the MIDI messages
local uart_port = 0

-- These are used by the receive function to keep track of the messages
local rcv = {}
rcv.in_message = false -- Tells if we are in the middle of a message
rcv.data_size = 0      -- Tells how many data bytes we expect
rcv.data_read = 0      -- Tells how many data bytes we already read
rcv.sysEx = false      -- If true, we are receiving a system exclusive message, keep reading until we get a defs[ "system_exclusive_end" ] byte

-- Holds the message we are reading
message = {}

-- Try to set the baud rate in the specified port ( uart )
-- returns true if successful or false otherwise
function init( port ) -- OK
    uart_port = port
    local actual_baud = uart.setup( port, defs[ "baud" ], defs[ "data_bits" ], defs[ "parity" ], defs[ "stop_bits" ] )
    assert( ( actual_baud >= 0.99 * defs[ "baud" ] ) and ( actual_baud < 1.01 * defs["baud"] ), 
            "Can't set the baud rate!" )
end

-- Internal function to split 14 bit data into 2 bytes
local function decode_14bit( n )  -- OK
  assert( ( n >= 0 ) and ( n <= 16383 ),
          "Invalid 14 bit number:  .. n .. " )

  local fine, coarse = 0,0
  local num = 8192
  while num >= 1 do 
    if n >= num then
      if num >= 128 then -- coarse byte
        coarse = coarse + ( num / 128 )
      else -- fine byte
        fine = fine + num
      end

      n = n - num
    end

      num = num / 2
    end
  
  return fine, coarse
end

-- Internal function to join 2 bytes into a 14 bit number
local function encode_14bit( fine, coarse ) -- OK
  -- Validate arguments
  assert( fine >= 0 and fine <= 127 and coarse >= 0 and coarse <= 127,
          "Arguments must be numbers between 0 and 127" )
 
  return fine + 128 * coarse
end

-- Internal function to validate a channel number
local function validate_channel( channel ) -- OK
  assert( channel >= 1 and channel <= 16, 
          "Argument must me a number between 1 and 16" )
end

-- Internal function to assemble a status package and send it
local function send_status( channel, message )  -- OK
  validate_channel( channel ) 

  channel = channel -1
 
  assert( message >= defs[ "note_off" ] and message <= defs[ "pitch_wheel" ], "Error: Invalid message code: " .. message )

  local package = message + channel

  uart.write( uart_port, package )
end

-- Internal function to split a status byte into message code and channel
local function split_status( status )
  local message, channel

  message = bit.band( status, 0xF0 )
  channel = bit.band( status, 0x0F ) + 1

  return message, channel
end

function send_control_change( channel, control, value ) -- OK
  validate_channel( channel ) 

  assert( control >= 0 and control <= 127, "Error: Control must be a number between 0 and 127" )
  assert( value >= 0 and value <= 127, "Error: Value must be a number between 0 and 127" )

  send_status( channel, defs[ "control_change" ] )
  uart.write( uart_port, control, value )
end

-- Sends a control change for a control with 14 bit precision
function send_14bit_control_change( channel, control_coarse, control_fine, value )  -- OK
  validate_channel( channel ) 

  assert( value >= 0 and value <= 16383, "Error: Value must be a number between 0 and 16383" )
  assert( control_coarse >= 0 and control_coarse <= 127 and control_fine >= 0 and control_fine <= 127,
          "Error: control id must be a number between 0 and 127" )

  local fine, coarse = decode_14bit( value )

  send_control_change( channel, control_coarse, coarse ) 
  send_control_change( channel, control_fine, fine )
end

-- Internal function to validate a system exclusive channel
local function validate_se_channel( channel ) -- OK
  assert( channel >= 1 and channel <= 128, "Error: The System Exclusive channel must be a number between 1 and 128" )
end

-- Internal function to validate a note number
local function validate_note( note ) -- OK
  assert( note >= 0 and note <= 127, 
     "Error: Note must be a numver between 0 and 127" )
end

-- Internal function that returns the number of data bytes of a message
-- Status is the Status Byte of the message
local function message_data_len( status )
  local index = 0
  local i = 128

  index = split_status( status ) 

  -- Compare with the MIDI constants
  local msg_types = {}
  msg_types[ defs[ "note_off" ] ] = 16
  msg_types[ defs[ "note_on" ] ] = 16
  msg_types[ defs[ "after_touch" ] ] = 16
  msg_types[ defs[ "control_change" ] ] = 16
  msg_types[ defs[ "program_change" ] ] = 8
  msg_types[ defs[ "channel_pressure" ] ] = 8
  msg_types[ defs[ "pitch_wheel" ] ] = 14
  msg_types[ defs[ "system_exclusive_begin" ] ] = defs[ "msg_size_unknown" ]

  return msg_types[ index ]
end

function message_data_size( status )
  local data_len = message_data_len( status )

  if data_len == 16 then -- 2 bytes
    return 2
  elseif data_len == 8 then -- 1 byte
    return 1
  elseif data_len == 14 then -- 14bit number
    return 1
  else  -- something odd or system exclusive
    return defs[ "msg_size_unknown" ]
  end
end

-- Internal function used by receive that returns the number of data bytes
-- expected for a message
local function message_data_bytes( status )
  local data_len = message_data_len( status )

  if data_len == 16 then -- 2 bytes
    return 2
  elseif data_len == 8 then -- 1 byte
    return 1
  elseif data_len == 14 then -- 14bit number
    return 2
  else  -- something odd or system exclusive
    return defs[ "msg_size_unknown" ]
  end
end

-- Internal function to send a note on or note off
local function send_note( channel, note, on, velocity )  -- OK
  assert( velocity >= 0 and velocity <= 127,
     "Error: Velocity must be a numver between 0 and 127" )
    
  if on then
    send_status( channel, defs[ "note_on" ] )
  else
    send_status( channel, defs[ "note_off" ] )
  end

  uart.write( uart_port, note, velocity )
end

-- Sends a note on message
function send_note_on( channel, note, velocity ) -- OK
  -- velocity = velocity or defs[ "default_note_velocity" ]
  velocity = velocity or 64
  send_note( channel, note, true, velocity )
end

-- Sends a note off message
function send_note_off( channel, note, velocity ) -- OK
  velocity = velocity or 64
  send_note( channel, note, false, velocity )
end

-- Sends an after touch / key pressure message
function send_after_touch( channel, note, pressure ) -- OK
  validate_channel( channel ) 
  validate_note( note ) 

  assert( pressure >= 0 and pressure <= 127,
     "Error: Pressure must be a number between 0 and 127" )
  
  send_status( channel, defs[ "after_touch" ] )
  uart.write( uart_port, note, pressure )
end

-- Sends a program / path / instrument / preset change message
function send_program_change( channel, program ) -- OK
  validate_channel( channel ) 

  assert( program >= 0 and program <= 127,
     "Error: Program must be a number between 0 and 127" )

  send_status( channel, defs[ "program_change" ] )
  uart.write( uart_port, program )
end

-- Sends a channel pressure message - sets the pressure for all notes of the channel
function send_channel_pressure( channel, pressure ) -- OK
  validate_channel( channel ) 

  assert( pressure >= 0 and pressure <= 127, 
    "Error: Pressure must be a number between 0 and 127" )

  send_status( channel, defs[ "channel_pressure" ] )
  uart.write( uart_port, pressure )
end

-- Sends a pitch wheel message
function send_pitch_wheel( channel, pitch ) -- OK
  validate_channel( channel ) 

  local fine, coarse = decode_14bit( pitch + defs[ "pitch_wheel_middle" ] )

  send_status( channel, defs[ "pitch_wheel" ] )
  uart.write( uart_port, fine, coarse )
end

-- Sends a System Exclusive stream - Use: download a memory
-- dump, send raw data, set custom parameters ...
-- All the bytes in data must not contain the bit #7 set !
-- The id is your Manufacturer's ID ( a number between 0 and 127 )
function send_system_exclusive( id, data )  -- Having some trouble
  assert( id >= 0 and id <= 127, 
    "Error: id must be a number between 0 and 127" )

  -- Validate data ( i.e look for bytes with the bit #7 set )
  assert( string.find( data, "[\128-\255]" ) == nil, "Error: Invalid byte in data" )

--  uart.write( uart_port, defs[ "system_exclusive_begin" ], id, data, defs[ "system_exclusive_end" ] )
  uart.write( uart_port, defs[ "system_exclusive_begin" ], id, data )
  uart.write( uart_port, defs[ "system_exclusive_end" ] )
end

-- Internal function to send a GM system enable / disable message
local function send_gm_system_enable_disable( channel, enable )
  local ed

  if enable then
    ed = 0x01 -- Enable code
  else
    ed = 0x00 -- Disable code
  end

  validate_se_channel( channel ) 
  channel = channel -1

  uart.write( uart_port, defs[ "system_exclusive_begin" ], defs[ "non_realtime_id" ], channel, defs[ "se_gm_system_enable_disable" ], ed, defs[ "system_exclusive_end" ] )
end

-- Sends a GM System Enable message
function send_gm_system_enable( channel )
  send_gm_system_enable_disable( channel, true )
end

-- Sends a GM System Disable message
function send_gm_system_disable( channel )
  send_gm_system_enable_disable( channel, false )
end

-- Sends a Master Volume message - Sets the device's Master Volume ( 14 bit - value )
function send_master_volume( channel, volume )
  -- Validate the volume
  assert( volume >= 0 and volume <= 16383,
    "Error: Volume must be a number between 0 and 16383" )

  validate_se_channel( channel ) 
  channel = channel -1

  local fine, coarse = decode_14bit( volume )

  uart.write( uart_port, defs[ "system_exclusive_begin" ], defs[ "realtime_id" ], channel, defs[ "se_device_control" ], defs[ "se_master_volume" ], fine, coarse, defs[ "system_exclusive_end" ] )
end

-- Sends a Quarter Frame Message - Used to keep slave in sync
function send_quarter_frame( time_code ) -- OK
  -- Validate time_code
  assert( time_code >= 0 and time_code <= 127,
    "Error: time_code must be a number between 0 and 127" )

  uart.write( uart_port, defs[ "quarter_frame" ], time_code )
end

-- Sends a Song Position Message - Tells the slaves to go to the specified song position
-- Note: Each MIDI Beat is a 16th note. Song starts at beat 0
function send_song_position( beat ) -- OK
  -- Validate the beat
  assert( beat >= 0 and beat <= 16383,
    "Error: beat must be a number between 0 and 16383" )

  local fine, coarse = decode_14bit( beat )

  uart.write( uart_port, defs[ "song_position" ], fine, coarse )
end

-- Sends a Song Select Message - Tells slaves to change to a specified song
function send_song_select( song ) -- OK
  -- Validate song
  assert( song >= 0 and song <= 127,
    "Error: Song must be a number between 0 and 127" )

  uart.write( uart_port, defs[ "song_select" ], song )
end

-- Sends a Tune Request Message - Tells the slave to perform a tuning calibration
function send_tune_request() -- OK
  uart.write( uart_port, defs[ "tune_request" ] )
end

-- Sends a MIDI Clock Message - Used to sync the slave and the master devices
-- Note: 
--   There are 24 MIDI Clocks in every quarter note.
--   1 bpm = 1 quarter note per minute
--   1 bmp = 24 Clocks per minute
function send_clock() -- OK
  uart.write( uart_port, defs[ "clock" ] )
end

-- Sends a MIDI Start Message - Tells slaves to play from the beginning
function send_start() -- OK
  uart.write( uart_port, defs[ "start" ] )
end

-- Sends a MIDI Continue Message - Tells slaves to play from the current position
-- Note: On stop, the devices must keep the current song position
function send_continue() -- OK
  uart.write( uart_port, defs[ "continue" ] )
end

-- Sends a MIDI Stop Message ( a.k.a pause ) - Tells slaves to stop playing
function send_stop() -- OK
  uart.write( uart_port, defs[ "stop" ] )
end

-- Sends a MIDI Active Sense Message - Tells other MIDI devices that the
-- connection is still active
function send_active_sense() -- OK
  uart.write( uart_port, defs[ "active_sense" ] )
end

-- Sends a Reset Message - Tells slaves to reset themselves to their default states
function send_reset() -- OK
  uart.write( uart_port, defs[ "reset" ] )
end

-- Reads data from uart and interprets it
-- Possible return values:
--   defs[ "msg_new_message" ] -> A new message was received and finished
--   defs[ "msg_in_message" ]  -> A new message was received, but is not complete
--   defs[ "msg_no_message" ]  -> Nothing or invalid data was received
--
-- The data read is stored in the message table:
--   message[ defs[ "msg_code" ] ]    -> MIDI code ( from defs table ) of the message
--   message[ defs[ "msg_channel" ] ] -> MIDI channel of the message
--   message[ defs[ "msg_data" ] ]    -> Data 1 parameter of the message
--   message[ defs[ "msg_data2" ] ]   -> Data 2 parameter of the message
--
-- Note: Not all messages have Data 1 or Data 2
-- Note 2: If a 14bit value is expectes, message[ defs[ "msg_data" ] ] will hold the 14bit value
-- Note 3: On system exclusive messages, message[ defs[ "msg_channel" ] ] is the ID
function receive( timeout, timer_id ) -- Still in test
  local buffer = ""   -- Buffer
  local c             -- Current character ( code )

  while true do
    c = uart.getchar( uart_port, timeout, timer_id ) -- Try to read a char

    if c ~= "" then
      c = string.byte( c ) -- If we got a char, get its code
    else
      if rcv.in_message then
        return defs[ "msg_in_message" ] -- If the message is incomplete, return in_message
      else
        return defs[ "msg_no_message" ] -- If we got nothing, return no_message
      end
    end

    if c > 127 then -- Begin of a message ( It's a Status byte )
      if c == defs[ "system_exclusive_end" ] then -- Check if it's the end of a system exclusive message before
        if rcv.in_message then                    -- erasing the buffer
          rcv.in_message = false
          rcv.data_read = 0
          return defs[ "msg_new_message" ]
        else
          rcv.in_message = false
          rcv.data_read = 0
          return defs[ "msg_no_message" ]
        end
      end

      rcv.in_message = true
      rcv.data_read = 0
      message = {}

      if c == defs[ "system_exclusive_begin" ] then -- System exclusive
        rcv.data_size = 0
        rcv.sysEx = true
        message[ defs[ "msg_data" ] ] = ""
        message[ defs[ "msg_code" ] ] = defs[ "system_exclusive_begin" ] -- System Exclusive messages don't have channel
      else  -- Not a system exclusive
        rcv.sysEx = false
        rcv.data_size = message_data_bytes( c ) 
        message[ defs[ "msg_code" ] ], message[ defs[ "msg_channel" ] ] = split_status( c ) -- Set message code and channel
      end
    end

    if rcv.in_message and c < 128 then -- We are receiving a message, store the data !
      -- If it's a system exclusive message ...
      if rcv.sysEx then
        -- Set the message[ defs[ "msg_channel" ] ] as the ID ( first byte )
        if rcv.data_read == 0 then
          message[ defs[ "msg_channel" ] ] = c
          rcv.data_read = 1
        else
          -- Concact the data
          message[ defs[ "msg_data" ] ] = message[ defs[ "msg_data" ] ] .. string.char( c )
        end
      else -- Not system exclusive
        if rcv.data_read == 0 then -- Received first byte
          message[ defs[ "msg_data"] ] = c
        end

        if rcv.data_read == 1 then -- Received second byte
          message[ defs[ "msg_data2"] ] = c
        end

        rcv.data_read = rcv.data_read + 1
        if rcv.data_read == rcv.data_size then -- Received all data expected
          rcv.in_message = false

          -- If data is a 14bit value, "concatenate" the data bytes
          if message_data_len( message[ defs[ "msg_code" ] ] ) == 14 then
            message[ defs[ "msg_data" ] ] = encode_14bit( message[ defs[ "msg_data" ] ], message[ defs[ "msg_data2" ] ] )
            message[ defs[ "msg_data2" ] ] = nil
          end

          return defs[ "msg_new_message" ]
        end
      end -- rcv.sysEx if
    end -- rcv.in_message if
  end -- while
end

-- Sends an all notes off message ( same as sending a note off message for each note on )
function send_all_notes_off( channel )
  send_control_change( channel, defs[ "cc_all_notes_off" ], 0 )
end

-- Sends an all sound off message ( stops all sounds instantly )
function send_all_sound_off( channel )
  send_control_change( channel, defs[ "cc_all_sound_off" ], 0 )
end

-- Sends an all controllers off message ( resets all controllers to the default values )
function send_all_controllers_off( channel )
  send_control_change( channel, defs[ "cc_all_controllers_off" ], 0 )
end

-- Sends an monophonic operation message ( changes to monophonic mode )
function send_mono_operation( channel )
  send_control_change( channel, defs[ "cc_mono_operation" ], 0 )
end

-- Sends an polyphonic operation message ( changes to polyphonic mode )
function send_poly_operation( channel )
  send_control_change( channel, defs[ "cc_poly_operation" ], 0 )
end

-- TESTED UP TO HERE  --

