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
char midi_init( char port )
{
  uart_port = port;
  u32 actual_baud = platform_uart_setup( port, baud, databits, parity, stop_bits );

  return ( actual_baud >= 0.99 * baud ) && ( actual_baud <= 1.01 * baud );
}

// Is n a 7bit number?
char midi_7bit( char n )
{
  return (( n >=0 ) && ( n <= 127 ));
}

// Internal function to split 14 bit data into 2 bytes
void midi_decode_14bit( int n, char *out )
{
  out[0] = n & 127;
  out[1] = ( n >> 7 ) & 127;
}

// Internal function to join 2 bytes into a 14 bit number
int midi_encode_14bit( fine, coarse )
{
  return fine + 128 * coarse;
}

// Internal function to validate a channel number
inline char elua_midi_validade_channel( char channel )
{
  return ( channel >= 1 ) && ( channel <= 16 );
}

// Internal function to assemble a status package and send it
void midi_send_status( char channel, char message )
{
  if (( message < note_off ) || ( message > pitch_wheel ))
    return;

  platform_uart_send( uart_port, message + channel );
}

// Internal function to split a status byte into message code and channel
// out: message, channel
void elua_midi_split_status( char status, char * out )
{
  out[0] = status & 0xF0;
  out[1] = ( status & 0x0F ) +1;
}

// Sends a control change message for a 7 bit control
void midi_send_control_change( char channel, char control, char value )
{
  if ( !midi_7bit( value ))
    return;

  if ( !midi_7bit( control ))
    return;

  midi_send_status( channel, control_change );
  platform_uart_write( uart_port, control );
  platform_uart_write( uart_port, value );
}


// Sends a control change for a control with 14 bit precision
void midi_send_14bit_control_change( char channel, char control_coarse, char control_fine, int value )
{
  char num[2];
  
  if (( value < 0 ) || ( value > 16383 ))
    return;

  if ( !midi_7bit( control_coarse ))
    return;

  if ( !midi_7bit( control_fine ))
    return;

  midi_decode_14bit( value, num );

  midi_send_control_change( channel, control_coarse, num[1] );
  midi_send_control_change( channel, control_fine, num[0] );
}

// Internal function to validate a system exclusive channel
char midi_validate_se_channel( char channel )
{
  return (( note >=0 ) && ( note <= 128 ));
}


// Internal function that returns the number of data bytes of a message
// Status is the Status Byte of the message
char midi_message_data_len( char status )
{
  char ss[2];
  midi_split_status( status, ss );

  switch ( ss[0] )
  {
    case note_off: return 16;
    case note_on: return 16;
    case after_touch: return 16;
    case control_change: return 16;
    case program_change: return 8;
    case channel_pressure: return 8;
    case pitch_wheel: return 14;
    case system_exclusive_begin: return msg_size_unknown;
  }
}

char midi_message_data_size( char status )
{
  char data_len = midi_message_data_len( status );

  if ( data_len == 16 )
    return 2;
  else if ( data_len == 8 )
    return 1;
  else if ( data_len == 14 )
    return 1;
  else
    return msg_size_unknown;
}

// Internal function used by receive that returns the number of data bytes
// expected for a message
char message_data_bytes( char status )
{
  char data_len = midi_message_data_len( status );

  if ( data_len == 16 )
    return 2;
  else if ( data_len == 8 )
    return 1;
  else if ( data_len == 14 )
    return 2;
  else
    return msg_size_unknown;
}

// Internal function to send a note on or note off
 send_note( channel, note, on, velocity )  -- OK
const LUA_REG_TYPE eluamidi_map[] = {
  { LSTRKEY( "init" ), LFUNCVAL( midi_init_lua ) },
  { LSTRKEY( "write" ), LFUNCVAL( midi_write_lua ) },
  { LSTRKEY( "goto" ), LFUNCVAL( midi_goto_lua ) },
  { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_eluamidi( lua_State *L )
{
  LREGISTER( L, "eluamidi", eluamidi_map );
  return 1;
};

