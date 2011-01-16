i// #include "eluamidi.h"
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "platform.h"
#include "auxmods.h"
#include "lrotable.h"
#include "platform_conf.h"
#include <string.h>

#define MIN_OPT_LEVEL 2
#include "lrodefs.h"


// Define uart configuration
#define baud 31250
#define data_bits 8
#define stop_bits PLATFORM_UART_STOPBITS_1
#define parity PLATFORM_UART_PARITY_NONE

// Midi messages
#define note_off 0x80
#define note_on 0x90
#define after_touch 0xA0
#define key_pressure defs[ "after_touch" ]
#define control_change 0xB0
#define program_change 0xC0
#define channel_pressure 0xD0
#define pitch_wheel 0xE0
#define system_exclusive_begin 0xF0
#define system_exclusive_end 0xF7

#define pitch_wheel_middle 8192
#define default_note_velocity 64

// Midi IDs ( generic system excluseve IDs )
#define realtime_id 0x7F
#define non_realtime_id 0x7E
#define educational_id 0x7D

// Midi System Exclusive Sub-IDs
#define se_gm_system_enable_disable 0x09
#define se_device_control 0x04
#define se_master_volume 0x01

// Midi Time Messages
#define tm_quarter_frame 0xF1
#define tm_song_position 0xF2
#define tm_song_select 0xF3
#define tm_tune_request 0xF6
#define tm_clock 0xF8
#define tm_start 0xFA
#define tm_continue 0xFB
#define tm_stop 0xFC
#define tm_active_sense 0xFE
#define tm_reset 0xFF

// Message table index
#define msg_code 1
#define msg_channel 2
#define msg_data 3
#define msg_data2 4

// Midi Receive return codes
#define msg_new_message 1
#define msg_in_message 2
#define msg_no_message 3
#define msg_size_unknown -1

// Midi default controllers ( for especial messages )
#define cc_all_notes_off 123
#define cc_all_sound_off 120
#define cc_all_controllers_off 121
#define cc_mono_operation 126
#define cc_poly_operation 127

// Holds the uart ID to which send the MIDI messages
char uart_port = 0;

// Try to set the baud rate in the specified port ( uart )
// returns 1 if successful or 0 otherwise
char eluamidi_init( char port )
{
  uart_port = port;
  u32 actual_baud = platform_uart_setup( port, baud, databits, parity, stop_bits );

  return ( actual_baud >= 0.99 * baud ) && ( actual_baud <= 1.01 * baud );
}

// Internal function to split 14 bit data into 2 bytes
void decode_14bit( int n, char *out )
{
  out[0] = n & 127;
  out[1] = ( n >> 7 ) & 127;
}

// Internal function to join 2 bytes into a 14 bit number
int encode_14bit( fine, coarse )
{
  return fine + 128 * coarse;
}

const LUA_REG_TYPE eluamidi_map[] = {
  { LSTRKEY( "init" ), LFUNCVAL( eluamidi_init_lua ) },
  { LSTRKEY( "write" ), LFUNCVAL( eluamidi_write_lua ) },
  { LSTRKEY( "goto" ), LFUNCVAL( eluamidi_goto_lua ) },
  { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_eluamidi( lua_State *L )
{
  LREGISTER( L, "eluamidi", eluamidi_map );
  return 1;
};

