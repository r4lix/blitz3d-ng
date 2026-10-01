#include <vector>
#include <cmath>
#include <algorithm>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../stdutil/stdutil.h"
#include "runtime.sdl.h"
#include <bb/pixmap/pixmap.h>
#include <bb/event/event.h>
#include <bb/system/system.h>
#include <bb/input/input.h>
#include <bb/hook/hook.h>
#ifdef BB_NDK
#include <bb/system/system.ndk.h>
#endif

#include <SDL_syswm.h>

#include <map>

#include "scancodes.cpp"

#include <bb/input/input.h>

class SDLInputDriver : public BBInputDriver{
public:
	~SDLInputDriver(){}

	BBDevice *getJoystick( int port )const{ return 0; }
	int getJoystickType( int port )const{ return 0; }
	int numJoysticks()const{ return 0; }

	int toAscii( int code )const{
		return code&0xff;
	}
};

class SDLJoystick : public BBDevice{
private:
	SDL_Joystick *js;
public:
	SDLJoystick( SDL_Joystick *js ):js(js){
		memset( axis_states,0,sizeof(axis_states) );
		memset( down_state,0,sizeof(down_state) );

		SDL_JoystickGetGUIDString( SDL_JoystickGetGUID(js),id,sizeof(id) );
		snprintf( name,sizeof(name),"%s",SDL_JoystickName( js ) );
	}

	void update(){
		int ax_count=SDL_JoystickNumAxes( js );
		if( ax_count>32 ) ax_count=32;

		for( int i=0;i<ax_count;i++ ){
			axis_states[i]=(float)SDL_JoystickGetAxis( js,i )/SHRT_MAX;
		}

		int btn_count=SDL_JoystickNumButtons( js );
		for( int i=0;i<btn_count;i++ ){
			setDownState( i,SDL_JoystickGetButton( js,i ) );
		}
	}
};

std::map<SDL_Window*,SDLRuntime*> runtimes;

BBRuntime *bbCreateOpenGLRuntime(){
	return d_new SDLRuntime();
}

SDLRuntime::SDLRuntime(){
	// runtimes.insert( make_pair( wnd,this ) );
	// bbAppOnChange.add( _refreshTitle,this );
}

SDLRuntime::~SDLRuntime(){
	// bbAppOnChange.del( _refreshTitle,this );
	SDL_Quit();
}

void SDLRuntime::afterCreate(){
	SDL_InitSubSystem( SDL_INIT_JOYSTICK );

	gx_input=d_new SDLInputDriver();

	for( int i=0;i<SDL_NumJoysticks();i++ ){
		SDL_Joystick *js=SDL_JoystickOpen( i );
		if( js ){
			SDLJoystick *j=d_new SDLJoystick( js );
			bbJoysticks.push_back( j );
		}
	}

#ifdef BB_NDK
	bbSetJNI( (jobject)SDL_AndroidGetActivity(),(JNIEnv*)SDL_AndroidGetJNIEnv() );
#endif

	BBContextDriver::change( "sdl" );
	bbDefaultGraphics();
}

void SDLRuntime::asyncStop(){
}

void SDLRuntime::asyncRun(){
}

void SDLRuntime::asyncEnd(){
}

// Debug aid: BB_INJECT="3000:key:44;6000:move:640,400;6200:click:1" pushes
// synthetic SDL events when the given number of milliseconds have elapsed.
// key:<SDL scancode> sends a press and release, click:<1|2|3> a button press.
static void injectEvents(){
	struct Item{ Uint32 at;std::string kind;int a,b;bool done; };
	static std::vector<Item> items;
	static bool parsed=false;
	if( !parsed ){
		parsed=true;
		if( const char *env=getenv( "BB_INJECT" ) ){
			std::string all=env;
			size_t pos=0;
			while( pos<all.size() ){
				size_t end=all.find( ';',pos );
				if( end==std::string::npos ) end=all.size();
				std::string t=all.substr( pos,end-pos );
				pos=end+1;
				int at=0,a=0,b=0;
				char kind[16]={0};
				if( sscanf( t.c_str(),"%d:%15[a-z]:%d,%d",&at,kind,&a,&b )>=3 ){
					items.push_back( Item{ (Uint32)at,kind,a,b,false } );
				}
			}
		}
	}
	Uint32 now=SDL_GetTicks();
	for( Item &i:items ){
		if( i.done || now<i.at ) continue;
		i.done=true;
		SDL_Event e;
		memset( &e,0,sizeof(e) );
		if( i.kind=="key" ){
			e.type=SDL_KEYDOWN;e.key.keysym.scancode=(SDL_Scancode)i.a;e.key.state=SDL_PRESSED;
			SDL_PushEvent( &e );
			e.type=SDL_KEYUP;e.key.state=SDL_RELEASED;
			SDL_PushEvent( &e );
		}else if( i.kind=="move" ){
			e.type=SDL_MOUSEMOTION;e.motion.x=i.a;e.motion.y=i.b;
			SDL_PushEvent( &e );
		}else if( i.kind=="click" ){
			e.type=SDL_MOUSEBUTTONDOWN;e.button.button=i.a==2?SDL_BUTTON_RIGHT:i.a==3?SDL_BUTTON_MIDDLE:SDL_BUTTON_LEFT;e.button.state=SDL_PRESSED;
			SDL_PushEvent( &e );
			e.type=SDL_MOUSEBUTTONUP;e.button.state=SDL_RELEASED;
			SDL_PushEvent( &e );
		}
	}
}

#include "input_delivery.inc"

void *SDLRuntime::window(){
	// audio drivers ask for the window while the runtime is still starting up
	if( !bbContextDriver ) return 0;
	auto graphics=(SDLGraphics*)((SDLContextDriver*)bbContextDriver)->getGraphics();
	if( !graphics ) return 0;
#ifdef WIN32
	SDL_SysWMinfo info;
	SDL_VERSION( &info.version );
	SDL_GetWindowWMInfo( graphics->wnd,&info );
	return info.info.win.window;
#else
	return 0;
#endif
}

void SDLRuntime::moveMouse( int x,int y ){
	if( !bbContextDriver ) return;
	auto graphics=(SDLGraphics*)((SDLContextDriver*)bbContextDriver)->getGraphics();
	graphics->moveMouse( x,y );
	// SDL cannot warp a pointer that does not exist (Switch), so tell the engine directly
	padEmulation.pointerMoved( x,y );
	deliverMouseMove( x,y );
}

extern bool bbPointerVisible; // graphics: draws a software cursor when one is needed

void SDLRuntime::setPointerVisible( bool vis ){
	bbPointerVisible=vis;
	SDL_ShowCursor( vis?SDL_ENABLE:SDL_DISABLE );
}

void SDLRuntime::_refreshTitle( void *data,void *context ){
	((SDLRuntime*)context)->setTitle( ((BBApp*)data)->title.c_str() );
}

void SDLRuntime::setTitle( const char *title ){
	// SDL_SetWindowTitle( wnd,title );
}

BBMODULE_CREATE( runtime_sdl ){
	return true;
}

BBMODULE_DESTROY( runtime_sdl ){
	return true;
}
