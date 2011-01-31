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
inline char midi_7bit( char n )
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
inline int midi_encode_14bit( fine, coarse )
{
  return fine + 128 * coarse;
}

// Internal function to validate a channel number
inline char midi_validate_channel( char channel )
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
void midi_split_status( char status, char * out )
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
  return (( channel >=0 ) && ( channel <= 128 ));
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
char midi_message_data_bytes( char status )
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
void midi_send_note( char channel, char note, char on, char velocity )
{
  if ( on )
    midi_send_status( channel, note_on );
  else
    midi_send_status( channel, note_off );

  platform_uart_send( uart_port, note );
  platform_uart_send( uart_port, velocity );
}

// Sends a note on message
void midi_send_note_on( char channel, char note, char velocity )
{
  midi_send_note( channel, note, 1, velocity );
}

// Sends a note off message
void midi_send_note_off( char channel, char note, char velocity )
{
  midi_send_note( channel, note, 0, velocity );
}

// Sends an after touch / key pressure message
void midi_send_after_touch( char channel, char note, char pressure )
{
  if ( !midi_validate_channel( channel ) )
    return;

  if ( !midi_7bit( note ) )
    return;

  midi_send_status( channel, after_touch );
  platform_uart_send( uart_port, note );
  platform_uart_send( uart_port, pressure );
}

// Sends a program / path / instrument / preset change message
void midi_send_program_change( char channel, char program )
{
  if (!midi_validate_channel( channel ) )
    return;

  midi_send_status( channel, program_change );
  platform_uart_send( uart_port, program );
}

// Sends a channel pressure message - sets the pressure for all notes of the channel
void midi_send_channel_pressure( char channel, char pressure )
{
  if ( !midi_validate_channel( channel ) )
    return;

  midi_send_status( channel, channel_pressure );
  platform_uart_send( uart_port, pressure );
}

// Sends a pitch wheel message
void midi_send_pitch_wheel( char channel, char pitch )
{
  if ( !midi_validate_channel( channel ) )
    return;

  char decoded[2];

  midi_decode_14bit( pitch + pitch_wheel_middle, decoded );

  midi_send_status( channel, pitch_wheel )
  platform_uart_send( uart_port, decoded[0] );
  platform_uart_send( uart_port, decoded[1] );
}

// Sends a System Exclusive stream - Use: download a memory
// dump, send raw data, set custom parameters ...
// All the bytes in data must not contain the bit #7 set !
// The id is your Manufacturer's ID ( a number between 0 and 127 )
void midi_send_system_exclusive( char id, char * data, char data_size )  // Having some trouble
{
  // Validate ID
  if ( !midi_7bit( id ))
    return;

  // Validate data ( i.e look for bytes with the bit #7 set )
  int i;
  for ( i=0; i<data_size; i++)
    if ( data[i] & 128 )
      return;

  // Send Data
  platform_uart_send( uart_port, system_exclusive_begin ) ;
  platform_uart_send( uart_port, id );

  for ( i=0; i<data_size; i++ )
    platform_uart_send( uart_port, data[i] );

  platform_uart_send( uart_port, system_exclusive_end );
}

// Internal function to send a General MIDI system enable / disable message
void midi_send_gm_system_enable_disable( char channel, char enable )
{  
  char ed;

  if ( enable )
    ed = 0x01; // Enable code
  else
    ed = 0x00; // Disable code

  if ( !midi_validate_se_channel( channel ) )
    return;

  channel = channel -1
  
  platform_uart_send( uart_port, system_exclusive_begin );
  platform_uart_send( uart_port, non_realtime_id );
  platform_uart_send( uart_port, channel );
  platform_uart_send( uart_port, se_gm_system_enable_disable );
  platform_uart_send( uart_port, ed );
  platform_uart_send( uart_port, system_exclusive_end );
}

// Sends a General MIDI System Enable message
void midi_send_gm_system_enable( char channel )
{
  midi_send_gm_system_enable_disable( channel, true );
}

// Sends a General MIDI System Disable message
void midi_send_gm_system_disable( char channel )
{
  midi_send_gm_system_enable_disable( channel, false );
}

// Sends a Master Volume message - Sets the device's Master Volume ( 14 bit - value )
void midi_send_master_volume( char channel, unsigned int volume )
{
  // Validate the volume
  if ( volume & ( 1 << 14 ))
    return;

  if ( !midi_validate_se_channel( channel ) )
    return;

  // Convert channel
  channel = channel -1;

  // Split 14 bit into 2 bytes
  char out[2];
  midi_decode_14bit( volume, out );

  platform_uart_send( uart_port, system_exclusive_begin );
  platform_uart_send( uart_port, realtime_id );
  platform_uart_send( uart_port, channel );
  platform_uart_send( uart_port, se_device_control );
  platform_uart_send( uart_port, se_master_volume );
  platform_uart_send( uart_port, out[0] );
  platform_uart_send( uart_port, out[1] );
  platform_uart_send( uart_port, system_exclusive_end );
}

// Sends a Quarter Frame Message - Used to keep slave in sync
void midi_send_quarter_frame( char time_code )
{
  // Validate time_code
  if ( !midi_7bit( time_code ) )
    return;

  platform_uart_send( uart_port, tm_quarter_frame );
  platform_uart_send( uart_port, time_code );
}

// Sends a Song Position Message - Tells the slaves to go to the specified song position
// Note: Each MIDI Beat is a 16th note. Song starts at beat 0
void midi_send_song_position( int beat )
{
  // Validate the beat
  if ( beat & ( 1 << 14 ))
    return;

  char out[2];
  midi_decode_14bit( beat, out );

  platform_uart_send( uart_port, tm_song_position );
  platform_uart_send( uart_port, out[0] );
  platform_uart_send( uart_port, out[1] );
}

// Sends a Song Select Message - Tells slaves to change to a specified song
void midi_send_song_select( char song )
{
  // Validate song
  if ( !midi_7bits( song ) )
    return;

  platform_uart_send( uart_port, tm_song_select );
  platform_uart_send( uart_port, song );
}

// Sends a Tune Request Message - Tells the slave to perform a tuning calibration
void midi_send_tune_request()
{
  platform_uart_send( uart_port, tm_tune_request );
}

// Sends a MIDI Clock Message - Used to sync the slave and the master devices
// Note: 
//   There are 24 MIDI Clocks in every quarter note.
//   1 bpm = 1 quarter note per minute
//   1 bmp = 24 Clocks per minute
void midi_send_clock()
{
  platform_uart_send( uart_port, tm_clock );
}

// Sends a MIDI Start Message - Tells slaves to play from the beginning
void midi_send_start()
{
  platform_uart_send( uart_port, tm_start );
}

// Sends a MIDI Continue Message - Tells slaves to play from the current position
// Note: On stop, the devices must keep the current song position
void midi_send_continue()
{
  platform_uart_send( uart_port, tm_continue );
}

// Sends a MIDI Stop Message ( a.k.a pause ) - Tells slaves to stop playing
void midi_send_stop()
{
  platform_uart_send( uart_port, tm_stop );
}

// Sends a MIDI Active Sense Message - Tells other MIDI devices that the
// connection is still active
void midi_send_active_sense()
{
  platform_uart_send( uart_port, tm_active_sense );
}

// Sends a Reset Message - Tells slaves to reset themselves to their default states
void midi_send_reset()
{
  platform_uart_send( uart_port, tm_reset );
}

// Reads data from uart and interprets it
// Possible return values:
//   defs[ "msg_new_message" ] -> A new message was received and finished
//   defs[ "msg_in_message" ]  -> A new message was received, but is not complete
//   defs[ "msg_no_message" ]  -> Nothing or invalid data was received
//
// The data read is stored in the message table:
//   message[ defs[ "msg_code" ] ]    -> MIDI code ( from defs table ) of the message
//   message[ defs[ "msg_channel" ] ] -> MIDI channel of the message
//   message[ defs[ "msg_data" ] ]    -> Data 1 parameter of the message
//   message[ defs[ "msg_data2" ] ]   -> Data 2 parameter of the message
//
// Note: Not all messages have Data 1 or Data 2
// Note 2: If a 14bit value is expectes, message[ defs[ "msg_data" ] ] will hold the 14bit value
// Note 3: On system exclusive messages, message[ defs[ "msg_channel" ] ] is the ID
char receive( int timeout, char timer_id )
{
  
}

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

