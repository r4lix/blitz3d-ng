
#include "../../../stdutil/slowlog.h"
#include "../../../stdutil/stdutil.h"
#include <bb/runtime/runtime.h>
#include "audio.h"

#include <string>

static inline void debugSound( BBSound *s ){
	if( bb_env.debug ){
		if( !gx_audio->verifySound( s ) ) RTEX( "Sound does not exist" );
	}
}

static BBSound *loadSound( BBStr *f,bool use_3d ){
	std::string t=canonicalpath(*f);delete f;
	return gx_audio ? gx_audio->loadSound( t,use_3d ) : 0;
}

static BBChannel *playMusic( BBStr *f,bool use_3d ){
	std::string t=canonicalpath(*f);delete f;
	return gx_audio ? gx_audio->playFile( t,use_3d ) : 0;
}

BBSound * BBCALL bbLoadSound( BBStr *f ){
	SLOWLOG("LoadSound",*f);
	return loadSound( f,false );
}

void BBCALL bbFreeSound( BBSound *sound ){
	if( !sound ) return;
	debugSound( sound );
	gx_audio->freeSound( sound );
}

void BBCALL bbLoopSound( BBSound *sound,bb_int_t loop ){
	if( !sound ) return;
	debugSound( sound );
	sound->setLoop( loop!=0 );
}

void BBCALL bbSoundPitch( BBSound *sound,bb_int_t pitch ){
	if( !sound ) return;
	debugSound( sound );
	sound->setPitch( pitch );
}

void BBCALL bbSoundVolume( BBSound *sound,bb_float_t volume ){
	if( !sound ) return;
	debugSound( sound );
	sound->setVolume( volume );
}

void BBCALL bbSoundPan( BBSound *sound,bb_float_t pan ){
	if( !sound ) return;
	debugSound( sound );
	sound->setPan( pan );
}

// volume: -2 leaves the channel alone, below 0 starts it paused (the game resumes it), else sets the volume
BBChannel * BBCALL bbPlaySound( BBSound *sound,bb_float_t volume ){
	SLOWLOG("PlaySound","");
	if( !sound ) return 0;
	debugSound( sound );
	BBChannel *c=sound->play();
	if( c && volume>-1.5f ){
		if( volume<0 ) c->setPaused( true );
		else c->setVolume( volume>1 ? 1 : volume );
	}
	return c;
}

BBChannel * BBCALL bbPlayMusic( BBStr *f,bb_int_t mode,bb_float_t volume ){
	BBChannel *c=playMusic( f,false );
	if( c ){
		if( mode&2 ) c->setLoop( true );
		if( volume>-1.5f ){
			if( volume<0 ) c->setPaused( true );
			else c->setVolume( volume>1 ? 1 : volume );
		}
	}
	return c;
}

void BBCALL bbChannelReverb( BBChannel *channel ){}

BBChannel * BBCALL bbPlayCDTrack( bb_int_t track,bb_int_t mode ){
	return gx_audio ? gx_audio->playCDTrack( track,mode ) : 0;
}

void BBCALL bbStopChannel( BBChannel *channel ){
	if( !channel ) return;
	channel->stop();
}

void BBCALL bbPauseChannel( BBChannel *channel ){
	if( !channel ) return;
	channel->setPaused( true );
}

void BBCALL bbResumeChannel( BBChannel *channel ){
	if( !channel ) return;
	channel->setPaused( false );
}

void BBCALL bbChannelPitch( BBChannel *channel,bb_int_t pitch ){
	if( !channel ) return;
	channel->setPitch( pitch );
}

void BBCALL bbChannelVolume( BBChannel *channel,bb_float_t volume ){
	if( !channel ) return;
	channel->setVolume( volume );
}

void BBCALL bbChannelPan( BBChannel *channel,bb_float_t pan ){
	if( !channel ) return;
	channel->setPan( pan );
}

bb_int_t BBCALL bbChannelPlaying( BBChannel *channel ){
	return channel ? channel->isPlaying() : 0;
}

bb_float_t BBCALL bbChannelDuration( BBChannel *channel ){
	return channel ? channel->getDuration() : 0;
}

bb_float_t BBCALL bbChannelPosition( BBChannel *channel ){
	return channel ? channel->getPosition() : 0;
}

BBSound * BBCALL bbLoad3DSound( BBStr *f ){
	SLOWLOG("Load3DSound",*f);
	return loadSound( f,true );
}

void pauseAudio( void *data,void *context ){
	if( gx_audio ) gx_audio->setPaused( true );
}

void resumeAudio( void *data,void *context ){
	if( gx_audio ) gx_audio->setPaused( false );
}

BBMODULE_CREATE( audio ){
	gx_audio=0;
	bbRuntimeOnSuspend->add( pauseAudio,0 );
	bbRuntimeOnResume->add( resumeAudio,0 );
	return true;
}

BBMODULE_DESTROY( audio ){
	if( gx_audio ){
		delete gx_audio;
		gx_audio=0;
	}
	return true;
}
