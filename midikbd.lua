dofile"/rom/keyboard.lc"
require"eluaeluamidi"

mbed.pio.configpin( mbed.pio.P13, 1, 0, 0 )
eluaeluamidi.init(1)
keyboard.configkeys( keyboard.USE, keyboard.IGNORE )

function kbd()
  
  -- Set scan code set to 2
  keyboard.setscancodeset( 2 )

  -- Disable typematic repeat
  -- keyboard.configkeys( keyboard.USE, keyboard.IGNORE )

  local b1, b2
  local down = {}
  local channel = 1
  local velocity = 64
  local first = 60 -- First note on the left ( A key ) -> 60 == Middle C
  local offsets = {}

  -- Mapping between keys and note offsets
  offsets[ 0x1C ] = 0
  offsets[ 0x1D ] = 1
  offsets[ 0x1B ] = 2
  offsets[ 0x23 ] = 3
  offsets[ 0x2D ] = 4
  offsets[ 0x2B ] = 5
  offsets[ 0x2C ] = 6
  offsets[ 0x34 ] = 7
  offsets[ 0x33 ] = 8
  offsets[ 0x3C ] = 9
  offsets[ 0x3B ] = 10
  offsets[ 0x43 ] = 11
  offsets[ 0x42 ] = 12
  offsets[ 0x44 ] = 13
  offsets[ 0x4B ] = 14
  offsets[ 0x4C ] = 15
  offsets[ 0x54 ] = 16
  offsets[ 0x52 ] = 17
  offsets[ 0x5b ] = 18
  offsets[ 0x5d ] = 19

  -- Tells wich keys are held down ( in case we receive typematic repeat )
  down[ 0x1C ] = false
  down[ 0x1D ] = false
  down[ 0x1B ] = false
  down[ 0x23 ] = false
  down[ 0x2D ] = false
  down[ 0x2B ] = false
  down[ 0x2C ] = false
  down[ 0x34 ] = false
  down[ 0x33 ] = false
  down[ 0x3C ] = false
  down[ 0x3B ] = false
  down[ 0x43 ] = false
  down[ 0x42 ] = false
  down[ 0x44 ] = false
  down[ 0x4B ] = false
  down[ 0x4C ] = false
  down[ 0x54 ] = false
  down[ 0x52 ] = false
  down[ 0x5b ] = false
  down[ 0x5d ] = false

  while true do
    -- Get a message byte
    b1 = keyboard.receive()

    -- If it's a break code...
    if b1 == 0xF0 then
      -- receive the key code
      b2 = keyboard.receive()
  
      if offsets[ b2 ] ~= nil then
        -- update down table
        down[ b2 ] = false

        -- Send midi message
        eluamidi.send_note_off( channel, first + offsets[ b2 ], velocity )
      end
    else
      if offsets[ b2 ] ~= nil then
        -- If key key was not already down...
        if down[ b1 ] == false then
          -- update the down table
          down[ b1 ] = true

          -- Send midi message
          eluamidi.send_note_on( channel, first + offsets[ b1 ], velocity )
        end
      end
    end
  end
end

