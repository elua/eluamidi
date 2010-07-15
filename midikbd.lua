
dofile"/rom/keyboard.lc"
require"eluamidi"

mbed.pio.configpin( mbed.pio.P13, 1, 0, 0 )
eluamidi.init(1)
keyboard.configkeys( keyboard.USE, keyboard.IGNORE )

function kbd()
  
  -- Set scan code set to 3
  keyboard.setscancodeset( 3 )

  -- Disable typematic repeat
  keyboard.configkeys( keyboard.USE, keyboard.IGNORE )

  local b1, b2             -- Received Bytes 1 and 2
--  local down = {}          -- List of keys held down ( for typematic repeat )
  local channel = 1        -- Which channel messages will be sent to
  local velocity = 64      -- Which velocity to send on note messages
  local first = 60         -- First note on the left ( A key ) -> 60 == Middle C
  local offsets = {}       -- Tells which key triggers which note
  local sliderCoarse = 16  -- Current selected control code ( coarse )
  local sliderFine = 16    -- Current selected control code ( fine )
  local sliderBits = 7     -- Number of bits for selected control
  local max7bit = 0x7F     -- Max 7 bit control value
  local max14bit = 0x3FFF  -- Max 14 bit control value
  local ctrlValue = {}     -- Tells which key sends which control value %
  local ctrlCoarse = {}    -- Coarse / 7 bit control ID for key
  local ctrlFine = {}      -- Fine control ID for key
  local ctrlBits = {}      -- Tells the number of bits of the control

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
  offsets[ 0x5B ] = 18
  offsets[ 0x5C ] = 19

  -- 1 ~ 4 -> Control Change
  ctrlValue[ 0x16 ] = 0
  ctrlValue[ 0x1E ] = 0.33
  ctrlValue[ 0x26 ] = 0.66
  ctrlValue[ 0x25 ] = 1

  -- Num Pad 1 ~ 6 -> Control Select
  --   1 ~ 3
  ctrlCoarse[ 0x69 ] = 16
  ctrlCoarse[ 0x72 ] = 17
  ctrlCoarse[ 0x7A ] = 18

  --   4 ~ 6
  ctrlCoarse[ 0x6B ] = 12
  ctrlFine[ 0x6B ] = 44
  ctrlCoarse[ 0x73 ] = 13
  ctrlFine[ 0x73 ] = 45
  ctrlCoarse[ 0x74 ] = 10
  ctrlFine[ 0x74 ] = 42

  ctrlBits[ 0x69 ] = 7
  ctrlBits[ 0x72 ] = 7
  ctrlBits[ 0x7A ] = 7
  ctrlBits[ 0x6B ] = 14
  ctrlBits[ 0x73 ] = 14
  ctrlBits[ 0x74 ] = 14


  while true do
    -- Get a message byte
    b1 = keyboard.receive()

    -- If it's a break code...
    if b1 == 0xF0 then
      -- receive the key code
      b2 = keyboard.receive()
  
      -- If it's a note, send the note off message
      if offsets[ b2 ] ~= nil then
        eluamidi.send_note_off( channel, first + offsets[ b2 ], velocity )
      else
        -- If ESC key, end function
        if b2 == 0x08 then
          return
        end
      end
    else -- Make code
      -- If it's a note, send the note on message
      if offsets[ b1 ] ~= nil then
        eluamidi.send_note_on( channel, first + offsets[ b1 ], velocity )
      else
        -- Control change
        if ctrlValue[ b1 ] ~= nil then
          if sliderBits == 7 then
            eluamidi.send_control_change( channel, sliderCoarse, max7bit * ctrlValue[ b1 ] )
          else
            eluamidi.send_14bit_control_change( channel, sliderCoarse, sliderFine, max14bit * ctrlValue[ b1 ] )
          end
        else
          -- Selected a new control
          if ctrlCoarse[ b1 ] ~= nil then 
            sliderCoarse = ctrlCoarse[ b1 ]
            sliderBits = ctrlBits[ b1 ]

            if sliderBits == 14 then
              sliderFine = ctrlFine[ b1 ]
            end -- If
          end -- If
        end -- Else ( control change )
      end -- Else ( note )
    end -- Else ( break code )
  end -- While
end




