// #include "eluamidi.h"
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "platform.h"
#include "auxmods.h"
#include "lrotable.h"
#include "platform_conf.h"
#include <string.h>
#include <stdlib.h>

#define MIN_OPT_LEVEL 2
#include "lrodefs.h"


// Define uart configuration
#define baud 31250
#define databits 8
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
inline int midi_encode_14bit( char fine, char coarse )
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
  platform_uart_send( uart_port, control );
  platform_uart_send( uart_port, value );
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
inline void midi_send_note( char channel, char note, char on, char velocity )
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

  midi_send_status( channel, pitch_wheel );
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

  channel = channel -1;
  
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
  midi_send_gm_system_enable_disable( channel, 1 );
}

// Sends a General MIDI System Disable message
void midi_send_gm_system_disable( char channel )
{
  midi_send_gm_system_enable_disable( channel, 0 );
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
  if ( !midi_7bit( song ) )
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
//   msg_new_message -> A new message was received and finished
//   msg_in_message  -> A new message was received, but is not complete
//   msg_no_message  -> Nothing or invalid data was received
//
// The data read is stored in the out vector ( this will be allocated by this
// function ):
//   out[0]    -> MIDI code ( from defs table ) of the message
//   out[1]    -> MIDI channel of the message / ID in system exclusive messages
//   out[2]    -> Data 1 parameter of the message
//   out[3]    -> Data 2 parameter of the message
//
// Note: Not all messages have Data 1 or Data 2
// Note 2: If a 14bit value is expected, out[2] will hold the fine value and
// out[3] the coarse value. Use the midi_encode_14bit function to turn that into
// a 14bit number.
// Note 3: Usert must free the *out vector
char receive( int timeout, char timer_id, char ** out )
{
  static char in_message = 0;
  static char sysEx = 0;
  static unsigned int data_read = 0;
  static unsigned int data_size = 0;
  static unsigned int buffer_size = 0;


//  char * buffer;
  char c; // Current character
  int tmp;

  while (1)
  {
    tmp = platform_s_uart_recv( timer_id, timeout );

    if ( tmp != -1 )
      c = (char)tmp; // We got a char
    else
      if ( in_message )
        return msg_in_message; // Message is incomplete
      else
        return msg_no_message; // There is no message

    if ( c & 128 ) // Begin of a message ( status byte )
    {
      if ( c == system_exclusive_end ) // Check if it's the end of a system exclusive message before
      {                                // erasing the buffer
        if ( in_message )
        {
          in_message = 0;
          data_read = 0;

          // Add the sysEx end byte so the user knows where the message ends
          if ( data_read == buffer_size ) // Out vector is full
            *out = realloc( *out, buffer_size + 2 ); // Note size of out before realloc is buffer_size +1

          *out[ buffer_size +1 ] = system_exclusive_end;

          return msg_new_message;
        }
        else
        {
          in_message = 0;
          data_read = 0;
          if ( *out != NULL )
            free( *out );

          *out = NULL;
          return msg_no_message;
        }
      }

      in_message = 1;
      data_read = 0;

      if ( c == system_exclusive_begin ) // SysEx message
      {
        data_size = 0;
        sysEx = 1;
        *out = (char *)malloc( 10 * sizeof( char ));
        memset( *out, 0, 10 ); // Fill vector with 0s
        buffer_size = 9; // size of the out vector minus 1 ( Message Type byte )
        *out[0] = system_exclusive_begin;
      }
      else // Non SysEx message
      {
        sysEx = 0;
        *out = (char *)malloc( 4 * sizeof( char ));
        memset( *out, 0, 4 ); // Fill vector with 0s
        buffer_size = 3; // size of the out vector minus 1 ( Message Type byte )
        data_size = midi_message_data_bytes( c );
        midi_split_status( c, *out ); // Fill message type and channel values
        data_read = 1;
      }
    }

    if (( in_message ) && ( c < 128 )) // We are receiving a message, store the data !
    {
      // If it's a system exclusive message ...
      if ( sysEx )
      {
        // Is it the ID byte ?
        if ( data_read == 0 )
        {
          *out[1] = c;
          data_read = 1;
        }
        else
        {
          // Concact the data
          if ( data_read == buffer_size ) // Out vector is full
          {
            *out = realloc( *out, buffer_size + 10 );
            memset( *out + data_read + 1, 0, 10 ); // Fill new space with 0s
            buffer_size += 10;
          }

          data_read ++;
          *out[data_read] = c;
        }
      }
      else // Non SysEx message
      {
        data_read ++;
        *out[data_read]  = c;

        if ( data_read -1 == data_size )
        {
          in_message = 0;
          return msg_new_message;
        }
      } // Non SysEx
    } // Receiving a message if
  } // While
}

// Sends an all notes off message ( same as sending a note off message for each note on )
void midi_send_all_notes_off( char channel )
{
  midi_send_control_change( channel, cc_all_notes_off, 0 );
}

// Sends an all sound off message ( stops all sounds instantly )
void midi_send_all_sound_off( char channel )
{
  midi_send_control_change( channel, cc_all_sound_off, 0 );
}

// Sends an all controllers off message ( resets all controllers to the default values )
void midi_send_all_controllers_off( char channel )
{
  midi_send_control_change( channel, cc_all_controllers_off, 0 );
}

// Sends an monophonic operation message ( changes to monophonic mode )
void midi_send_mono_operation( char channel )
{
  midi_send_control_change( channel, cc_mono_operation, 0 );
}

// Sends an polyphonic operation message ( changes to polyphonic mode )
void midi_send_poly_operation( char channel )
{
  midi_send_control_change( channel, cc_poly_operation, 0 );
}

// -------- BIND ----------
// Lua: midi.init( uart_id )
int midi_init_lua( lua_State *L )
{
  midi_init( luaL_checkinteger( L, 1 ) );
  return 0;
}

// Lua: midi.send_control_change( channel, control_id, value )
int midi_send_control_change_lua( lua_State *L )
{
  midi_send_control_change( luaL_checkinteger( L, 1 ), luaL_checkinteger( L, 2 ), luaL_checkinteger( L, 3 ) );
  return 0;
}

// Lua: midi.send_14bit_control_change( channel, coarse_control_id, fine_control_id, value )
int midi_send_14bit_control_change_lua( lua_State *L )
{
  midi_send_14bit_control_change( luaL_checkinteger( L, 1 ), luaL_checkinteger( L, 2 ), luaL_checkinteger( L, 3 ), luaL_checkinteger( L, 4 ) );
  return 0;
}

// Lua: midi.send_note_on( channel, note, velocity )
int midi_send_note_on_lua( lua_State *L )
{
  midi_send_note_on( luaL_checkinteger( L, 1 ), luaL_checkinteger( L, 2 ), luaL_checkinteger( L, 3 ) );
  return 0;
}

// Lua: midi.send_note_off( channel, note, velocity )
int midi_send_note_off_lua( lua_State *L )
{
  midi_send_note_off( luaL_checkinteger( L, 1 ), luaL_checkinteger( L, 2 ), luaL_checkinteger( L, 3 ) );
  return 0;
}

// Lua: midi.send_after_touch( channel, note, pressure )
int midi_send_after_touch_lua( lua_State *L )
{
  midi_send_after_touch( luaL_checkinteger( L, 1 ), luaL_checkinteger( L, 2 ), luaL_checkinteger( L, 3 ) );
  return 0;
}

// Lua: midi.send_program_change( channel, program )
int midi_send_program_change_lua( lua_State *L )
{
  midi_send_program_change( luaL_checkinteger( L, 1 ), luaL_checkinteger( L, 2 ) );
  return 0;
}
  
// Lua: midi.send_channel_pressure( channel, pressure )
int midi_send_channel_pressure_lua( lua_State *L )
{
  midi_send_channel_pressure( luaL_checkinteger( L, 1 ), luaL_checkinteger( L, 2 ) );
  return 0;
}
  
// Lua: midi.send_pitch_wheel( channel, pitch )
int midi_send_pitch_wheel_lua( lua_State *L )
{
  midi_send_pitch_wheel( luaL_checkinteger( L, 1 ), luaL_checkinteger( L, 2 ) );
  return 0;
}

// Lua: midi.send_pitch_wheel( channel, pitch )
int midi_send_pitch_wheel_lua( lua_State *L )
{
  midi_send_pitch_wheel( luaL_checkinteger( L, 1 ), luaL_checkinteger( L, 2 ) );
  return 0;
}

// Lua: midi.send_system_exclusive( id, data, data_size )
int midi_send_system_exclusive_lua( lua_State *L )
{
  midi_send_system_exclusive( luaL_checkinteger( L, 1 ), luaL_checkstring( L, 2 ), luaL_checkinteger( L, 3 ) );
  return 0;
}

// Lua: midi.send_gm_system_enable( channel )
int midi_send_gm_system_enable_lua( lua_State *L )
{
  midi_send_gm_system_enable( luaL_checkinteger( L, 1 ) );
  return 0;
}

// Lua: midi.send_gm_system_disable( channel )
int midi_send_gm_system_disable_lua( lua_State *L )
{
  midi_send_gm_system_disable( luaL_checkinteger( L, 1 ) );
  return 0;
}

// Lua: midi.send_master_volume( channel, volume )
int midi_send_master_volume_lua( lua_State *L )
{
  midi_send_master_volume( luaL_checkinteger( L, 1 ) );
  return 0;
}

// Lua: midi.send_quarter_frame( time_code )
int midi_send_quarter_frame_lua( lua_State *L )
{
  midi_send_quarter_frame( luaL_checkinteger( L, 1 ) & 0x7F );
  return 0;
}

// Lua: midi.send_song_position( beat )
int midi_send_song_position_lua( lua_State *L )
{
  midi_send_song_position( luaL_checkinteger( L, 1 ) );
  return 0;
}

// Lua: midi.send_song_select( song )
int midi_send_song_select_lua( lua_State *L )
{
  midi_send_song_select( luaL_checkinteger( L, 1 ) & 0x7F );
  return 0;
}


const LUA_REG_TYPE eluamidi_map[] = {
  { LSTRKEY( "init" ), LFUNCVAL( midi_init_lua ) },
  { LSTRKEY( "send_control_change" ), LFUNCVAL( midi_send_control_change_lua ) },
  { LSTRKEY( "send_14bit_control_change" ), LFUNCVAL( midi_send_14bit_control_change_lua ) },
  { LSTRKEY( "send_note_on" ), LFUNCVAL( midi_send_note_on_lua ) },
  { LSTRKEY( "send_note_off" ), LFUNCVAL( midi_send_note_off_lua ) },
  { LSTRKEY( "send_after_touch" ), LFUNCVAL( midi_send_after_touch_lua ) },
  { LSTRKEY( "send_program_change" ), LFUNCVAL( midi_send_program_change_lua ) },
  { LSTRKEY( "send_channel_pressure" ), LFUNCVAL( midi_send_channel_pressure_lua ) },
  { LSTRKEY( "send_pitch_wheel" ), LFUNCVAL( midi_send_pitch_wheel_lua ) },
  { LSTRKEY( "send_system_exclusive" ), LFUNCVAL( midi_send_system_exclusive_lua ) },
  { LSTRKEY( "send_gm_system_enable" ), LFUNCVAL( midi_send_gm_system_enable_lua ) },
  { LSTRKEY( "send_gm_system_disable" ), LFUNCVAL( midi_send_gm_system_disable_lua ) },
  { LSTRKEY( "send_master_volume" ), LFUNCVAL( midi_send_master_volume_lua ) },
  { LSTRKEY( "send_quarter_frame" ), LFUNCVAL( midi_send_quarter_frame_lua ) },
  { LSTRKEY( "send_song_position" ), LFUNCVAL( midi_send_song_position_lua ) },
  { LSTRKEY( "send_song_select" ), LFUNCVAL( midi_send_select_lua ) },
  { LNILKEY, LNILVAL }
};

LUALIB_API int luaopen_eluamidi( lua_State *L )
{
  LREGISTER( L, "eluamidi", eluamidi_map );
  return 1;
};

