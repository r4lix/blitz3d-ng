#include <vector>
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

bool SDLRuntime::idle(){
	SDL_Event event;
	injectEvents();
	while( SDL_PollEvent(&event) ){
		if( event.type == SDL_QUIT ){
			RTEX( 0 );
		}else if( event.type==SDL_WINDOWEVENT ){
			if( event.window.event==SDL_WINDOWEVENT_RESIZED ) {
			}
		}else if( event.type==SDL_MOUSEMOTION ){
			BBEvent ev( BBEVENT_MOUSEMOVE,0,event.motion.x,event.motion.y );
			bbOnEvent.run( &ev );
		}else if( event.type==SDL_MOUSEBUTTONDOWN||event.type==SDL_MOUSEBUTTONUP ){
			int button=0;
			switch( event.button.button ){
			case SDL_BUTTON_LEFT:button=1;break;
			case SDL_BUTTON_MIDDLE:button=3;break;
			case SDL_BUTTON_RIGHT:button=2;break;
			}

			if( button ){
				BBEvent ev( event.type==SDL_MOUSEBUTTONDOWN?BBEVENT_MOUSEDOWN:BBEVENT_MOUSEUP,button );
				bbOnEvent.run( &ev );
			}
		}else if( (event.type==SDL_KEYDOWN||event.type==SDL_KEYUP) && event.key.repeat==0 ){
			int code=event.key.keysym.scancode;
			if( code>=MAX_SDL_SCANCODES ) continue;

			int key=SDL_SCANCODE_MAP[code];
			if( !key ){
				LOGD( "unmapped key code: %i",code );
				continue;
			}

			BBEvent ev;
			switch( event.type ){
			case SDL_KEYDOWN:
				ev=BBEvent( BBEVENT_KEYDOWN,key );
				break;
			case SDL_KEYUP:
				ev=BBEvent( BBEVENT_KEYUP,key );
				break;
			default:
				continue;
			}
			bbOnEvent.run( &ev );

			if( event.type==SDL_KEYDOWN ){
				BBEvent ev=BBEvent( BBEVENT_CHAR,0 );
				// LOGD( "code=%i",code );
				switch( code ){
				case SDL_SCANCODE_BACKSPACE:
					ev.data='\b';
					break;
				case SDL_SCANCODE_RETURN:case SDL_SCANCODE_RETURN2:case SDL_SCANCODE_KP_ENTER:
					ev.data='\n';
					break;
				case SDL_SCANCODE_UP:
					ev.data=BBInputDriver::ASC_UP;
					break;
				case SDL_SCANCODE_DOWN:
					ev.data=BBInputDriver::ASC_DOWN;
					break;
				case SDL_SCANCODE_LEFT:
					ev.data=BBInputDriver::ASC_LEFT;
					break;
				case SDL_SCANCODE_RIGHT:
					ev.data=BBInputDriver::ASC_RIGHT;
					break;
				}
				if( ev.data ) bbOnEvent.run( &ev );
			}
		}else if( event.type==SDL_TEXTINPUT||event.type==SDL_TEXTEDITING ){
			// LOGD( "text: %s",event.text.text );
			char *c=event.text.text;
			while( *c ){
				BBEvent ev=BBEvent( BBEVENT_CHAR,*(c++) );
				bbOnEvent.run( &ev );
			}
		}
	}

	return true;
}

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
}

void SDLRuntime::setPointerVisible( bool vis ){
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
