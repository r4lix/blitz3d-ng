// Software audio mixer for BBAudioDriver.
//
// Sounds are decoded to 16-bit PCM the first time they are played and then shared
// between voices (games such as SCP: Containment Breach load ~900 sounds up front, so
// decoding at load time would take gigabytes). Music and other streams (playFile) are
// decoded on the fly. Everything is mixed in one callback with linear resampling for
// pitch, per-voice volume/pan/loop, and simple distance attenuation for 3D sounds.
//
// Output goes to an SDL audio device. For headless runs BB_AUDIO_DUMMY=1 drives the
// mixer from a timer thread instead, and BB_AUDIO_DUMP=file.wav also records the mix.

#define NOMINMAX
#include "../stdutil/stdutil.h"
#include "audio.mixer.h"
#include <bb/audio/driver.h>
#include <bb/audio/ogg_stream.h>
#include <bb/audio/wav_stream.h>

#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#ifndef _WIN32
#include <pthread.h>
#endif
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

namespace{

const int kRate=44100;
const int kChunk=16384; // bytes per decoder read

struct PCM{
	std::vector<int16_t> samples; // interleaved
	unsigned channels=0,rate=0;
	size_t frames()const{ return channels ? samples.size()/channels : 0; }
};

AudioStream *openStream( const std::string &filename ){
	const char *ext=strrchr( filename.c_str(),'.' );
	if( !ext ) return 0;
	// like the other drivers: try the given extension, then the other common one
	const char *tries[]={ ext,strcasecmp( ext+1,"wav" )==0 ? ".ogg" : ".wav",0 };
	for( int i=0;tries[i];i++ ){
		AudioStream *s=0;
		if( strcasecmp( tries[i]+1,"ogg" )==0 ) s=new OGGAudioStream( kChunk );
		else if( strcasecmp( tries[i]+1,"wav" )==0 ) s=new WAVAudioStream( kChunk );
		if( !s ) continue;
		if( i==0 ){
			if( s->init( filename.c_str() ) ) return s;
		}else{
			std::string alt=filename.substr( 0,ext-filename.c_str() )+tries[i];
			if( s->init( alt.c_str() ) ) return s;
		}
		delete s;
	}
	return 0;
}

// append decoded bytes (8 or 16 bit) as 16-bit samples
void appendPCM( std::vector<int16_t> &out,const unsigned char *buf,size_t bytes,unsigned bits ){
	if( bits==16 ){
		size_t n=bytes/2;
		size_t old=out.size();
		out.resize( old+n );
		memcpy( &out[old],buf,n*2 );
	}else if( bits==8 ){
		for( size_t i=0;i<bytes;i++ ) out.push_back( (int16_t)(((int)buf[i]-128)<<8) );
	}
}

std::shared_ptr<PCM> decodeAll( const std::string &path ){
	AudioStream *s=openStream( path );
	if( !s ) return 0;
	AudioStream::Ref *r=s->getRef();
	auto pcm=std::make_shared<PCM>();
	pcm->channels=r->getChannels();
	pcm->rate=r->getFrequency();
	const unsigned bits=r->getBits();
	if( pcm->channels<1 || pcm->channels>2 || (bits!=8&&bits!=16) ){
		delete r;delete s;
		return 0;
	}
	for(;;){
		unsigned char *buf;
		size_t n=r->decode( &buf );
		if( n==0 ) break;
		appendPCM( pcm->samples,buf,n,bits );
	}
	delete r;
	delete s;
	return pcm->frames() ? pcm : 0;
}

class Mixer;

class Voice : public BBChannel{
public:
	Mixer *mixer;

	// sound voices share decoded PCM; stream voices decode as they go
	std::shared_ptr<PCM> pcm;
	AudioStream *stream=0;
	AudioStream::Ref *ref=0;
	std::vector<int16_t> buf; // stream voices: decoded audio ahead of `pos`
	bool streamEnded=false;
	unsigned bits=16;

	unsigned channels=1,rate=kRate;
	double pos=0; // in source frames
	double consumed=0;
	float volume=1.0f,pan=0.0f;
	int pitchHz=0;
	bool loop=false,paused=false;
	bool pending=false; // sound still being decoded on the worker thread; silent until it is
	std::atomic<bool> playing{ true };

	bool is3d=false;
	float p3[3]={ 0,0,0 };

	Voice( Mixer *m ):mixer( m ){}
	~Voice(){ release(); }

	void release(){
		pcm.reset();
		if( ref ){ delete ref;ref=0; }
		if( stream ){ delete stream;stream=0; }
		std::vector<int16_t>().swap( buf );
	}

	size_t availableFrames()const{
		return pcm ? pcm->frames() : buf.size()/channels;
	}

	const int16_t *data()const{
		return pcm ? pcm->samples.data() : buf.data();
	}

	// stream voices: keep at least a couple of frames decoded beyond `pos`
	void refill(){
		if( !stream || streamEnded ) return;
		// drop what has been consumed, keeping one frame of history for interpolation
		size_t used=(size_t)pos;
		if( used>1 ){
			size_t drop=used-1;
			buf.erase( buf.begin(),buf.begin()+drop*channels );
			pos-=drop;
		}
		while( buf.size()/channels < (size_t)pos+4096 ){
			unsigned char *raw;
			size_t n=ref->decode( &raw );
			if( n==0 ){
				if( loop && !restarted ){
					delete ref;
					ref=stream->getRef();
					restarted=true;
					continue;
				}
				streamEnded=true;
				break;
			}
			restarted=false;
			appendPCM( buf,raw,n,bits );
		}
	}
	bool restarted=false;

	void stop(){ playing=false; }
	void setPaused( bool p ){ paused=p; }
	void setPitch( int hz ){ pitchHz=hz; }
	void setVolume( float v ){ volume=v<0 ? 0 : v; }
	void setPan( float p ){ pan=std::max( -1.0f,std::min( 1.0f,p ) ); }
	void setLoop( bool l ){ loop=l; }
	void set3d( const float pos3[3],const float vel[3] ){
		is3d=true;
		p3[0]=pos3[0];p3[1]=pos3[1];p3[2]=pos3[2];
	}
	bool isPlaying(){ return playing; }
	float getDuration(){
		if( pcm ) return pcm->frames()/(float)pcm->rate;
		if( stream && ref ) return ref->getSamples()/(float)rate;
		return 0;
	}
	float getPosition(){ return (float)(consumed/rate); }
};

class Mixer{
public:
	std::mutex lock;
	std::vector<Voice*> active;
	std::deque<Voice*> all; // handles stay valid for the life of the program
	float master=1.0f;
	bool masterPaused=false;

	float listenerPos[3]={ 0,0,0 },listenerRight[3]={ 1,0,0 };
	float rolloff=1.0f,distScale=1.0f;

	void add( Voice *v ){
		std::lock_guard<std::mutex> g( lock );
		all.push_back( v );
		active.push_back( v );
	}

	void setListener( const float pos[3],const float fwd[3],const float up[3] ){
		std::lock_guard<std::mutex> g( lock );
		memcpy( listenerPos,pos,sizeof(listenerPos) );
		// Blitz is left-handed (+x right, +y up, +z forward): right = up x forward
		float r[3]={ up[1]*fwd[2]-up[2]*fwd[1],up[2]*fwd[0]-up[0]*fwd[2],up[0]*fwd[1]-up[1]*fwd[0] };
		float len=sqrtf( r[0]*r[0]+r[1]*r[1]+r[2]*r[2] );
		if( len>1e-6f ){ r[0]/=len;r[1]/=len;r[2]/=len; }
		memcpy( listenerRight,r,sizeof(r) );
	}

	// Mix `frames` stereo frames into out (interleaved int16).
	void mix( int16_t *out,int frames ){
		std::lock_guard<std::mutex> g( lock );
		static std::vector<float> acc;
		acc.assign( (size_t)frames*2,0.0f );

		if( !masterPaused ){
			for( size_t vi=0;vi<active.size(); ){
				Voice *v=active[vi];
				if( !v->playing ){
					v->release();
					active[vi]=active.back();active.pop_back();
					continue;
				}
				if( !v->paused && !v->pending ) mixVoice( v,acc.data(),frames );
				if( !v->playing ){
					v->release();
					active[vi]=active.back();active.pop_back();
					continue;
				}
				vi++;
			}
		}

		for( int i=0;i<frames*2;i++ ){
			float s=acc[i]*master;
			// soft limiter: many overlapping voices should saturate gently, not hard-clip
			const float knee=0.8f;
			float a=fabsf( s );
			if( a>knee ) s=(s<0 ? -1.0f : 1.0f)*(knee+(1.0f-knee)*tanhf( (a-knee)/(1.0f-knee) ));
			out[i]=(int16_t)(s*32767.0f);
		}
	}

	void mixVoice( Voice *v,float *acc,int frames ){
		float gain=v->volume,panv=v->pan;
		if( v->is3d ){
			float d[3]={ v->p3[0]-listenerPos[0],v->p3[1]-listenerPos[1],v->p3[2]-listenerPos[2] };
			float dist=sqrtf( d[0]*d[0]+d[1]*d[1]+d[2]*d[2] )/distScale;
			gain/=1.0f+rolloff*std::max( 0.0f,dist-1.0f );
			if( dist>1e-4f ){
				float side=(d[0]*listenerRight[0]+d[1]*listenerRight[1]+d[2]*listenerRight[2])/(dist*distScale);
				panv=std::max( -1.0f,std::min( 1.0f,panv+side ) );
			}
		}
		const float lg=gain*std::min( 1.0f,1.0f-panv )/32768.0f,rg=gain*std::min( 1.0f,1.0f+panv )/32768.0f;

		const double step=(v->pitchHz>0 ? (double)v->pitchHz : (double)v->rate)/kRate;
		const unsigned ch=v->channels;

		for( int f=0;f<frames;f++ ){
			if( v->stream ) v->refill();
			const size_t avail=v->availableFrames();
			size_t i=(size_t)v->pos;
			if( i>=avail ){
				if( v->pcm && v->loop && avail>0 ){
					v->pos=fmod( v->pos,(double)avail );
					i=(size_t)v->pos;
				}else{
					v->playing=false;
					return;
				}
			}
			const int16_t *d=v->data();
			float frac=(float)(v->pos-i);
			size_t j=i+1;
			if( j>=avail ) j=(v->pcm && v->loop) ? 0 : i;
			float l0,r0,l1,r1;
			if( ch==1 ){
				l0=r0=d[i];l1=r1=d[j];
			}else{
				l0=d[i*2];r0=d[i*2+1];l1=d[j*2];r1=d[j*2+1];
			}
			float l=l0+(l1-l0)*frac,r=r0+(r1-r0)*frac;
			acc[f*2]+=l*lg;
			acc[f*2+1]+=r*rg;
			v->pos+=step;
			v->consumed+=step;
		}
	}
};

// Sounds are decoded on first use. Decoding a long OGG takes 100+ ms, so it happens on a
// worker thread: the voice is created at once and stays silent until its audio is ready.
struct SoundData{
	std::shared_ptr<PCM> pcm;
	bool queued=false,failed=false;
	std::vector<Voice*> waiting;
};

struct DecodeJob{ std::shared_ptr<SoundData> data;std::string path;Mixer *mixer; };

class DecodeWorker{
	std::mutex m;
	std::condition_variable cv;
	std::deque<DecodeJob> jobs;
public:
	void run(){
		for(;;){
			DecodeJob j;
			{
				std::unique_lock<std::mutex> g( m );
				cv.wait( g,[this]{ return !jobs.empty(); } );
				j=jobs.front();jobs.pop_front();
			}
			std::shared_ptr<PCM> pcm=decodeAll( j.path );
			finish( j,pcm );
		}
	}
	DecodeWorker(){
#ifdef _WIN32
		std::thread( [this](){ run(); } ).detach();
#else
		// the default thread stack on the Switch is small and the decoders are not shy about stack
		pthread_attr_t attr;
		pthread_attr_init( &attr );
		pthread_attr_setstacksize( &attr,2*1024*1024 );
		pthread_t t;
		pthread_create( &t,&attr,[]( void *self )->void*{ ((DecodeWorker*)self)->run();return 0; },this );
		pthread_detach( t );
#endif
	}
	void finish( DecodeJob &j,std::shared_ptr<PCM> pcm );
	void push( const DecodeJob &j ){
		std::lock_guard<std::mutex> g( m );
		jobs.push_back( j );
		cv.notify_one();
	}
};

static DecodeWorker &decodeWorker(){
	static DecodeWorker *w=new DecodeWorker(); // never destroyed: the thread outlives static teardown
	return *w;
}

void DecodeWorker::finish( DecodeJob &j,std::shared_ptr<PCM> pcm ){
	std::lock_guard<std::mutex> g( j.mixer->lock );
	j.data->pcm=pcm;
	j.data->failed=!pcm;
	for( Voice *v:j.data->waiting ){
		if( !v->pending ) continue;
		if( pcm && v->playing ){
			v->pcm=pcm;
			v->channels=pcm->channels;
			v->rate=pcm->rate;
		}else v->playing=false;
		v->pending=false;
	}
	j.data->waiting.clear();
}

class MixerSound : public BBSound{
public:
	Mixer *mixer;
	std::string path;
	std::shared_ptr<SoundData> data=std::make_shared<SoundData>();
	bool loop=false;
	int pitchHz=0;
	float volume=1.0f,pan=0.0f;

	MixerSound( Mixer *m,const std::string &p ):mixer( m ),path( p ){}

	Voice *start( const float *pos3 ){
		std::shared_ptr<PCM> pcm;
		bool needQueue=false;
		Voice *v=new Voice( mixer );
		{
			std::lock_guard<std::mutex> g( mixer->lock );
			if( data->failed ){ delete v;return 0; }
			pcm=data->pcm;
			if( !pcm ){
				v->pending=true;
				data->waiting.push_back( v );
				needQueue=!data->queued;
				data->queued=true;
			}
		}
		if( needQueue ) decodeWorker().push( DecodeJob{ data,path,mixer } );
		if( pcm ){
			v->pcm=pcm;
			v->channels=pcm->channels;
			v->rate=pcm->rate;
		}
		v->loop=loop;
		v->pitchHz=pitchHz;
		v->volume=volume;
		v->pan=pan;
		if( pos3 ){
			v->is3d=true;
			memcpy( v->p3,pos3,sizeof(v->p3) );
		}
		mixer->add( v );
		return v;
	}

	BBChannel *play(){ return start( 0 ); }
	BBChannel *play3d( const float pos[3],const float vel[3] ){ return start( pos ); }
	void setLoop( bool l ){ loop=l; }
	void setPitch( int hz ){ pitchHz=hz; }
	void setVolume( float v ){ volume=v; }
	void setPan( float p ){ pan=p; }
};

class MixerDriver : public BBAudioDriver{
public:
	Mixer mixer;

	SDL_AudioDeviceID dev=0;
	std::thread dummy;
	std::atomic<bool> running{ false };
	FILE *dump=0;
	long dumpBytes=0;

	static void SDLCALL callback( void *user,Uint8 *stream,int len ){
		((MixerDriver*)user)->mixer.mix( (int16_t*)stream,len/4 );
	}

	void writeWavHeader(){
		if( !dump ) return;
		unsigned data=(unsigned)dumpBytes,riff=36+data,rate=kRate,byteRate=kRate*4;
		unsigned short fmt=1,ch=2,align=4,bits=16;
		unsigned fmtLen=16;
		fseek( dump,0,SEEK_SET );
		fwrite( "RIFF",1,4,dump );fwrite( &riff,4,1,dump );fwrite( "WAVEfmt ",1,8,dump );
		fwrite( &fmtLen,4,1,dump );fwrite( &fmt,2,1,dump );fwrite( &ch,2,1,dump );
		fwrite( &rate,4,1,dump );fwrite( &byteRate,4,1,dump );fwrite( &align,2,1,dump );fwrite( &bits,2,1,dump );
		fwrite( "data",1,4,dump );fwrite( &data,4,1,dump );
		fseek( dump,0,SEEK_END ); // the header is rewritten as audio is appended
		fflush( dump );
	}

	bool init(){
		const char *dummyEnv=getenv( "BB_AUDIO_DUMMY" ),*dumpPath=getenv( "BB_AUDIO_DUMP" );
		if( dumpPath ){
			dump=fopen( dumpPath,"wb" );
			writeWavHeader();
		}

		if( !dummyEnv && !dumpPath ){
			if( SDL_InitSubSystem( SDL_INIT_AUDIO )==0 ){
				SDL_AudioSpec want,have;
				SDL_zero( want );
				want.freq=kRate;
				want.format=AUDIO_S16SYS;
				want.channels=2;
				want.samples=1024;
				want.callback=callback;
				want.userdata=this;
				dev=SDL_OpenAudioDevice( 0,0,&want,&have,0 );
				if( dev && have.freq==kRate && have.channels==2 && have.format==AUDIO_S16SYS ){
					SDL_PauseAudioDevice( dev,0 );
					return true;
				}
				if( dev ){ SDL_CloseAudioDevice( dev );dev=0; }
			}
			LOGD( "audio: no usable device (%s), using a silent clock",SDL_GetError() );
		}

		// no device: advance the mixer in real time so durations and ChannelPlaying behave
		running=true;
		dummy=std::thread( [this](){
			std::vector<int16_t> out( 1024*2 );
			while( running ){
				mixer.mix( out.data(),1024 );
				if( dump ){
					fwrite( out.data(),4,1024,dump );
					dumpBytes+=1024*4;
					writeWavHeader();
				}
				std::this_thread::sleep_for( std::chrono::microseconds( 1000000LL*1024/kRate ) );
			}
		} );
		return true;
	}

	void shutdown(){
		if( dev ){ SDL_CloseAudioDevice( dev );dev=0; }
		if( dummy.joinable() ){ running=false;dummy.join(); }
		if( dump ){ writeWavHeader();fclose( dump );dump=0; }
	}

	~MixerDriver(){ shutdown(); }

	BBSound *loadSound( const std::string &filename,bool use_3d ){
		// fail like other drivers when the file is missing (decoding is deferred)
		FILE *f=fopen( filename.c_str(),"rb" );
		if( !f ){
			std::string alt=filename;
			size_t dot=alt.rfind( '.' );
			if( dot!=std::string::npos ) alt=alt.substr( 0,dot )+(strcasecmp( filename.c_str()+dot,".wav" )==0 ? ".ogg" : ".wav");
			f=fopen( alt.c_str(),"rb" );
			if( !f ) return 0;
			fclose( f );
			MixerSound *s=new MixerSound( &mixer,alt );
			sound_set.insert( s );
			return s;
		}
		fclose( f );
		MixerSound *s=new MixerSound( &mixer,filename );
		sound_set.insert( s );
		return s;
	}

	void setPaused( bool paused ){ std::lock_guard<std::mutex> g( mixer.lock );mixer.masterPaused=paused; }
	void setVolume( float volume ){ std::lock_guard<std::mutex> g( mixer.lock );mixer.master=volume; }

	void set3dOptions( float roll,float dopp,float dist ){
		std::lock_guard<std::mutex> g( mixer.lock );
		mixer.rolloff=roll;
		mixer.distScale=dist>0 ? dist : 1.0f;
	}

	void set3dListener( const float pos[3],const float vel[3],const float forward[3],const float up[3] ){
		mixer.setListener( pos,forward,up );
	}

	BBChannel *playCDTrack( int track,int mode ){ return 0; }

	BBChannel *playFile( const std::string &filename,bool use_3d ){
		AudioStream *s=openStream( filename );
		if( !s ) return 0;
		Voice *v=new Voice( &mixer );
		v->stream=s;
		v->ref=s->getRef();
		v->channels=v->ref->getChannels();
		v->rate=v->ref->getFrequency();
		v->bits=v->ref->getBits();
		if( v->channels<1 || v->channels>2 || (v->bits!=8&&v->bits!=16) ){
			delete v;
			return 0;
		}
		mixer.add( v );
		return v;
	}
};

MixerDriver *driver=0;

}

BBMODULE_CREATE( audio_mixer ){
	driver=d_new MixerDriver();
	if( !driver->init() ){
		delete driver;
		driver=0;
		return true;
	}
	gx_audio=driver;
	return true;
}

BBMODULE_DESTROY( audio_mixer ){
	if( driver ){
		driver->shutdown();
		if( gx_audio==driver ) gx_audio=0;
	}
	return true;
}
