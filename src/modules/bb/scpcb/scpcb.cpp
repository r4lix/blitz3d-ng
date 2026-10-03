#include <bb/blitz/blitz.h>
#include <bb/audio/audio.h>
#include <bb/audio/driver.h>
#include "scpcb.h"
#include <bb/bank/bank.h>
#include "../stdutil/stdutil.h"

#include <map>
#include <string>
#include <cstdio>

// ---------------------------------------------------------------------------
// FMOD 3 streams
//
// SCP:CB uses FMOD only for music and a handful of long streamed sounds. A
// "stream" is just a file name here; FSOUND_Stream_Play() starts it on the
// active BBAudioDriver and hands back an integer channel, as FMOD did.
// ---------------------------------------------------------------------------

namespace{
	struct Stream{
		std::string file;
		int mode;
	};

	std::map<int,Stream> streams;
	std::map<int,BBChannel*> channels;
	int next_stream=1,next_channel=1;

	BBChannel *findChannel( int id ){
		auto it=channels.find( id );
		return it==channels.end() ? 0 : it->second;
	}
}

extern "C"{

bb_int_t BBCALL bbFSOUND_Init( bb_int_t freq,bb_int_t chans,bb_int_t flags ){ return 1; }
bb_int_t BBCALL bbFSOUND_Close(){
	for( auto &c:channels ) if( c.second ) c.second->stop();
	channels.clear();
	return 1;
}

bb_int_t BBCALL bbFSOUND_Stream_Open( BBStr *filename,bb_int_t mode,bb_int_t memlength ){
	std::string f=*filename;delete filename;
	f=canonicalpath( f );
	FILE *fp=fopen( f.c_str(),"rb" );
	if( !fp ) return 0;
	fclose( fp );
	int id=next_stream++;
	streams[id]=Stream{ f,(int)mode };
	return id;
}

bb_int_t BBCALL bbFSOUND_Stream_Play( bb_int_t chan,bb_int_t stream ){
	auto it=streams.find( (int)stream );
	if( it==streams.end() || !gx_audio ) return -1;
	BBChannel *c=gx_audio->playFile( it->second.file,false );
	if( !c ) return -1;
	// FSOUND_LOOP_NORMAL (2) makes a stream repeat; music is opened this way
	c->setLoop( (it->second.mode&2)!=0 );
	int id=next_channel++;
	channels[id]=c;
	return id;
}

bb_int_t BBCALL bbFSOUND_Stream_Stop( bb_int_t stream ){ return 1; }
bb_int_t BBCALL bbFSOUND_Stream_Close( bb_int_t stream ){ streams.erase( (int)stream );return 1; }

void BBCALL bbFSOUND_StopSound( bb_int_t chan ){
	auto it=channels.find( (int)chan );
	if( it==channels.end() ) return;
	if( it->second ) it->second->stop();
	channels.erase( it );
}

void BBCALL bbFSOUND_SetVolume( bb_int_t chan,bb_int_t vol ){
	if( BBChannel *c=findChannel( (int)chan ) ) c->setVolume( vol/255.0f );
}
bb_int_t BBCALL bbFSOUND_SetVolumeAbsolute( bb_int_t chan,bb_int_t vol ){
	bbFSOUND_SetVolume( chan,vol );return 1;
}
bb_int_t BBCALL bbFSOUND_GetVolume( bb_int_t chan ){ return 255; }

void BBCALL bbFSOUND_SetPaused( bb_int_t chan,bb_int_t paused ){
	if( BBChannel *c=findChannel( (int)chan ) ) c->setPaused( paused!=0 );
}
bb_int_t BBCALL bbFSOUND_IsPlaying( bb_int_t chan ){
	BBChannel *c=findChannel( (int)chan );
	return c && c->isPlaying() ? 1 : 0;
}
bb_int_t BBCALL bbFSOUND_SetPan( bb_int_t chan,bb_int_t pan ){
	// FMOD: 0 = left, 255 = right, -1 = centre.
	if( BBChannel *c=findChannel( (int)chan ) ) c->setPan( pan<0 ? 0.0f : (pan-127.5f)/127.5f );
	return 1;
}

void BBCALL bbRT_Trace( BBStr *msg ){
	fprintf( stderr,"[t] %s\n",msg->c_str() );
	fflush( stderr );
	delete msg;
}

// ---------------------------------------------------------------------------
// On-screen keyboard (Switch software keyboard applet). Programs that have no keyboard
// call RT_TextPrompt$ instead of reading keys; elsewhere RT_HasPrompt() is 0 and typing works
// as usual.
// ---------------------------------------------------------------------------
bb_float_t BBCALL bbRT_SettingF( BBStr *key,bb_float_t def ){
	float v=bbSettingFloat( *key,(float)def );
	delete key;
	return v;
}

bb_int_t BBCALL bbRT_SettingI( BBStr *key,bb_int_t def ){
	int v=bbSettingInt( *key,(int)def );
	delete key;
	return v;
}

void BBCALL bbRT_PadInvBegin(){ bbPadSlotsBegin(); }
void BBCALL bbRT_PadInvSlot( bb_int_t x,bb_int_t y ){ bbPadSlotAdd( (int)x,(int)y ); }

void BBCALL bbRT_PadMenuBegin(){ bbPadMenuBegin(); }
void BBCALL bbRT_PadMenuRect( bb_int_t x,bb_int_t y,bb_int_t w,bb_int_t h ){ bbPadMenuAdd( (int)x,(int)y,(int)w,(int)h ); }

bb_int_t BBCALL bbRT_HasPrompt(){
#ifdef BB_NX
	return 1;
#else
	return 0;
#endif
}

BBStr * BBCALL bbRT_TextPrompt( BBStr *title,BBStr *initial ){
	std::string result=bbTextPrompt( *title,*initial );
	delete title;delete initial;
	return d_new BBStr( result );
}

BBStr * BBCALL bbRT_SettingS( BBStr *key,BBStr *def ){
	std::string v=bbSetting( *key,*def );
	delete key;delete def;
	return d_new BBStr( v );
}

// ---------------------------------------------------------------------------
// cpuid.dll: only shown on the debug CPU screen
// ---------------------------------------------------------------------------

BBStr * BBCALL bbCPUid(){ return d_new BBStr( "Unknown" ); }
BBStr * BBCALL bbCPUextendedId(){ return d_new BBStr( "Unknown" ); }
BBStr * BBCALL bbCPUbrand(){ return d_new BBStr( "Unknown" ); }
BBStr * BBCALL bbCPUfeatures(){ return d_new BBStr( "" ); }
bb_int_t BBCALL bbCPUmodel(){ return 0; }
bb_int_t BBCALL bbCPUfamily(){ return 0; }
bb_int_t BBCALL bbCPUsteppingId(){ return 0; }
bb_int_t BBCALL bbCPUl1cache(){ return 0; }
bb_int_t BBCALL bbCPUl2cache(){ return 0; }

// ---------------------------------------------------------------------------
// BlitzMovie: nothing opens, so the game skips the startup video
// ---------------------------------------------------------------------------

bb_int_t BBCALL bbBlitzMovie_Open( BBStr *n ){ delete n;return 0; }
void BBCALL bbBlitzMovie_Close(){}
bb_int_t BBCALL bbBlitzMovie_GetWidth(){ return 0; }
bb_int_t BBCALL bbBlitzMovie_GetHeight(){ return 0; }
bb_int_t BBCALL bbBlitzMovie_GetPixel( bb_int_t o ){ return 0; }
bb_int_t BBCALL bbBlitzMovie_Play(){ return 0; }
bb_int_t BBCALL bbBlitzMovie_Stop(){ return 0; }
bb_int_t BBCALL bbBlitzMovie_OpenD3D( BBStr *n,bb_int_t d,bb_int_t dd ){ delete n;return 0; }
bb_int_t BBCALL bbBlitzMovie_DrawD3D( bb_int_t x,bb_int_t y,bb_int_t w,bb_int_t h ){ return 0; }
bb_int_t BBCALL bbBlitzMovie_MuteAudio(){ return 0; }
bb_int_t BBCALL bbBlitzMovie_SetVolume( bb_int_t v ){ return 0; }
bb_int_t BBCALL bbBlitzMovie_IsPlaying(){ return 0; }
bb_int_t BBCALL bbBlitzMovie_OpenDecodeToImage( BBStr *n,bb_int_t b,bb_int_t l ){ delete n;return 0; }
bb_int_t BBCALL bbBlitzMovie_OpenDecodeToTexture( BBStr *n,bb_int_t b,bb_int_t l ){ delete n;return 0; }
bb_int_t BBCALL bbBlitzMovie_Pause(){ return 0; }

// ---------------------------------------------------------------------------
// user32 / kernel32. The game only uses these to manage its own window and to
// read free memory for the console; neither is meaningful on the platforms we
// target, so they report "focused, nothing to do".
// ---------------------------------------------------------------------------

// Fills a 16-byte bank with left,top,right,bottom. There is no desktop to
// query on every platform, so report a 1280x720 client area.
bb_int_t BBCALL bbapi_GetClientRect( bb_int_t h,bb_int_t rect ){
	bbBank *bank=(bbBank*)rect;
	if( !bank || bbBankSize( bank )<16 ) return 0;
	bbPokeInt( bank,0,0 );bbPokeInt( bank,4,0 );
	bbPokeInt( bank,8,1280 );bbPokeInt( bank,12,720 );
	return 1;
}
bb_int_t BBCALL bbapi_GetDesktopWindow(){ return 1; }
bb_int_t BBCALL bbapi_GetFocus(){ return 1; }
bb_int_t BBCALL bbapi_SetWindowLong( bb_int_t h,bb_int_t i,bb_int_t v ){ return 0; }
bb_int_t BBCALL bbapi_SetWindowPos( bb_int_t h,bb_int_t a,bb_int_t x,bb_int_t y,bb_int_t cx,bb_int_t cy,bb_int_t f ){ return 0; }
bb_int_t BBCALL bbapi_GetModuleFileName( bb_int_t m,BBStr *f,bb_int_t s ){ delete f;return 0; }
void BBCALL bbapi_GlobalMemoryStatus( void *b ){}
void BBCALL bbGlobalMemoryStatus( void *b ){}

// ---------------------------------------------------------------------------
// zlibwapi: only the self-updater touches it
// ---------------------------------------------------------------------------

BBStr * BBCALL bbZlibWapi_zError( bb_int_t c ){ return d_new BBStr( "" ); }
BBStr * BBCALL bbZlibWapi_ZlibVersion(){ return d_new BBStr( "" ); }
bb_int_t BBCALL bbZlibWapi_CompressBound( bb_int_t l ){ return l; }
bb_int_t BBCALL bbZlibWapi_Compress( bb_int_t a,bb_int_t b,bb_int_t c,bb_int_t d ){ return -1; }
bb_int_t BBCALL bbZlibWapi_Compress2( bb_int_t a,bb_int_t b,bb_int_t c,bb_int_t d,bb_int_t e ){ return -1; }
bb_int_t BBCALL bbZlibWapi_UnCompress( bb_int_t a,bb_int_t b,bb_int_t c,bb_int_t d ){ return -1; }
bb_int_t BBCALL bbZlibWapi_Adler32( bb_int_t a,bb_int_t b,bb_int_t c ){ return 0; }
bb_int_t BBCALL bbZlibWapi_Crc32( bb_int_t a,bb_int_t b,bb_int_t c ){ return 0; }
bb_int_t BBCALL bbZlibWapi_UnzOpen( BBStr *p ){ delete p;return 0; }
bb_int_t BBCALL bbZlibWapi_UnzClose( bb_int_t z ){ return 0; }
bb_int_t BBCALL bbZlibWapi_UnzGetGlobalInfo( bb_int_t z,bb_int_t i ){ return -1; }
bb_int_t BBCALL bbZlibWapi_UnzGetGlobalComment( bb_int_t z,bb_int_t c,bb_int_t l ){ return -1; }
bb_int_t BBCALL bbZlibWapi_UnzLocateFile( bb_int_t z,BBStr *n,bb_int_t c ){ delete n;return -1; }
bb_int_t BBCALL bbZlibWapi_UnzGoToFirstFile( bb_int_t z ){ return -1; }
bb_int_t BBCALL bbZlibWapi_UnzGoToNextFile( bb_int_t z ){ return -1; }
bb_int_t BBCALL bbZlibWapi_UnzGetCurrentFileInfo( bb_int_t z,bb_int_t i,bb_int_t n,bb_int_t ns,bb_int_t e,bb_int_t es,bb_int_t c,bb_int_t cs ){ return -1; }
bb_int_t BBCALL bbZlibWapi_UnzOpenCurrentFile( bb_int_t z ){ return -1; }
bb_int_t BBCALL bbZlibWapi_UnzOpenCurrentFilePassword( bb_int_t z,BBStr *p ){ delete p;return -1; }
bb_int_t BBCALL bbZlibWapi_UnzReadCurrentFile( bb_int_t z,bb_int_t b,bb_int_t l ){ return -1; }
bb_int_t BBCALL bbZlibWapi_UnzCloseCurrentFile( bb_int_t z ){ return -1; }
bb_int_t BBCALL bbZlibWapi_ZipOpen( BBStr *n,bb_int_t a ){ delete n;return 0; }
bb_int_t BBCALL bbZlibWapi_ZipClose( bb_int_t z,BBStr *c ){ delete c;return -1; }
bb_int_t BBCALL bbZlibWapi_ZipOpenNewFileInZip( bb_int_t z,BBStr *n,bb_int_t i,bb_int_t le,bb_int_t les,bb_int_t ge,bb_int_t ges,BBStr *c,bb_int_t m,bb_int_t l ){ delete n;delete c;return -1; }
bb_int_t BBCALL bbZlibWapi_ZipOpenNewFileInZip3( bb_int_t z,BBStr *n,bb_int_t i,bb_int_t le,bb_int_t les,bb_int_t ge,bb_int_t ges,BBStr *c,bb_int_t m,bb_int_t l,bb_int_t r,bb_int_t w,bb_int_t ml,bb_int_t s,BBStr *p,bb_int_t cr ){ delete n;delete c;delete p;return -1; }
bb_int_t BBCALL bbZlibWapi_ZipWriteFileInZip( bb_int_t z,bb_int_t d,bb_int_t l ){ return -1; }
bb_int_t BBCALL bbZlibWapi_ZipCloseFileInZip( bb_int_t z ){ return -1; }

}

BBMODULE_CREATE( scpcb ){ return true; }
BBMODULE_DESTROY( scpcb ){ return true; }
