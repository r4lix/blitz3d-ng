// --- input delivery ---------------------------------------------------------------
// Real SDL events and the gamepad emulation below both go through these, so the engine
// sees the same thing whichever device produced the input.

extern int bbPointerX,bbPointerY; // graphics: last pointer position the engine was told about

static void deliverKey( int code,bool down,bool synthetic ){
	if( code<0 || code>=MAX_SDL_SCANCODES ) return;

	int key=SDL_SCANCODE_MAP[code];
	if( !key ){
		LOGD( "unmapped key code: %i",code );
		return;
	}

	BBEvent ev=BBEvent( down ? BBEVENT_KEYDOWN : BBEVENT_KEYUP,key );
	bbOnEvent.run( &ev );

	if( !down ) return;

	BBEvent cev=BBEvent( BBEVENT_CHAR,0 );
	switch( code ){
	case SDL_SCANCODE_BACKSPACE:
		cev.data='\b';
		break;
	case SDL_SCANCODE_RETURN:case SDL_SCANCODE_RETURN2:case SDL_SCANCODE_KP_ENTER:
		cev.data='\n';
		break;
	case SDL_SCANCODE_UP:
		cev.data=BBInputDriver::ASC_UP;
		break;
	case SDL_SCANCODE_DOWN:
		cev.data=BBInputDriver::ASC_DOWN;
		break;
	case SDL_SCANCODE_LEFT:
		cev.data=BBInputDriver::ASC_LEFT;
		break;
	case SDL_SCANCODE_RIGHT:
		cev.data=BBInputDriver::ASC_RIGHT;
		break;
	case SDL_SCANCODE_SPACE:
		// real keyboards send text input for this; emulated ones do not
		if( synthetic ) cev.data=' ';
		break;
	}
	if( cev.data ) bbOnEvent.run( &cev );
}

static void deliverMouseButton( int button,bool down ){
	BBEvent ev( down ? BBEVENT_MOUSEDOWN : BBEVENT_MOUSEUP,button );
	bbOnEvent.run( &ev );
}

static void deliverMouseMove( int x,int y ){
	bbPointerX=x;bbPointerY=y;
	BBEvent ev( BBEVENT_MOUSEMOVE,0,x,y );
	bbOnEvent.run( &ev );
}

// --- gamepad -> keyboard/mouse ----------------------------------------------------
// SCP: Containment Breach (like most PC games) wants a keyboard and mouse. On the Switch
// the gamepad is translated to the keys the game binds by default (WASD, space to blink,
// shift to sprint, ctrl to crouch, tab for the inventory, F5 to save) and the mouse.
// Enabled on the Switch, or elsewhere with BB_PAD=1.
namespace{

struct PadEmulation{
	bool initialised=false,enabled=false;
	SDL_GameController *pad=0;
	bool held[SDL_NUM_SCANCODES]={ false };
	bool buttons[4]={ false };
	float px=0,py=0,carryX=0,carryY=0;
	Uint32 last=0;

	void open(){
		for( int i=0;i<SDL_NumJoysticks();i++ ){
			if( SDL_IsGameController( i ) ){
				pad=SDL_GameControllerOpen( i );
				if( pad ){
					LOGD( "gamepad: %s",SDL_GameControllerName( pad ) );
					return;
				}
			}
		}
	}

	void setKey( int sc,bool down ){
		if( held[sc]==down ) return;
		held[sc]=down;
		deliverKey( sc,down,true );
	}

	void setButton( int b,bool down ){
		if( buttons[b]==down ) return;
		buttons[b]=down;
		deliverMouseButton( b,down );
	}

	// the engine recentres the pointer itself (MoveMouse), so follow it
	void pointerMoved( int x,int y ){
		px=(float)x;py=(float)y;
	}

	void pump(){
		if( !initialised ){
			initialised=true;
#ifdef BB_NX
			enabled=true;
#else
			enabled=getenv( "BB_PAD" )!=0;
#endif
			if( enabled ){
				SDL_InitSubSystem( SDL_INIT_GAMECONTROLLER );
				SDL_GameControllerEventState( SDL_ENABLE );
				if( gx_graphics ){
					px=gx_graphics->getLogicalWidth()/2.0f;
					py=gx_graphics->getLogicalHeight()/2.0f;
				}
			}
		}
		if( !enabled ) return;

		if( pad && !SDL_GameControllerGetAttached( pad ) ){
			SDL_GameControllerClose( pad );
			pad=0;
		}
		if( !pad ) open();
		if( !pad ){
			// nothing connected: release anything we were holding
			for( int sc=0;sc<SDL_NUM_SCANCODES;sc++ ) setKey( sc,false );
			for( int b=1;b<4;b++ ) setButton( b,false );
			return;
		}

		auto btn=[&]( SDL_GameControllerButton b ){ return SDL_GameControllerGetButton( pad,b )!=0; };
		auto axis=[&]( SDL_GameControllerAxis a ){ return SDL_GameControllerGetAxis( pad,a )/32767.0f; };

		bool want[SDL_NUM_SCANCODES]={ false };

		// left stick walks (with hysteresis so it does not chatter at the threshold)
		const float lx=axis( SDL_CONTROLLER_AXIS_LEFTX ),ly=axis( SDL_CONTROLLER_AXIS_LEFTY );
		auto threshold=[&]( int sc ){ return held[sc] ? 0.30f : 0.45f; };
		want[SDL_SCANCODE_A]=lx<-threshold( SDL_SCANCODE_A );
		want[SDL_SCANCODE_D]=lx>threshold( SDL_SCANCODE_D );
		want[SDL_SCANCODE_W]=ly<-threshold( SDL_SCANCODE_W );
		want[SDL_SCANCODE_S]=ly>threshold( SDL_SCANCODE_S );

		// face buttons and shoulders
		const bool lmb=btn( SDL_CONTROLLER_BUTTON_A ) || axis( SDL_CONTROLLER_AXIS_TRIGGERRIGHT )>0.4f;
		const bool rmb=axis( SDL_CONTROLLER_AXIS_TRIGGERLEFT )>0.4f;
		if( btn( SDL_CONTROLLER_BUTTON_A ) ) want[SDL_SCANCODE_RETURN]=true; // "press any key", text boxes
		if( btn( SDL_CONTROLLER_BUTTON_B ) ) want[SDL_SCANCODE_LCTRL]=true;   // crouch
		if( btn( SDL_CONTROLLER_BUTTON_X ) ) want[SDL_SCANCODE_TAB]=true;     // inventory
		if( btn( SDL_CONTROLLER_BUTTON_Y ) || btn( SDL_CONTROLLER_BUTTON_RIGHTSHOULDER ) ) want[SDL_SCANCODE_SPACE]=true; // blink
		if( btn( SDL_CONTROLLER_BUTTON_LEFTSHOULDER ) || btn( SDL_CONTROLLER_BUTTON_LEFTSTICK ) ) want[SDL_SCANCODE_LSHIFT]=true; // sprint
		if( btn( SDL_CONTROLLER_BUTTON_START ) ) want[SDL_SCANCODE_ESCAPE]=true;
		if( btn( SDL_CONTROLLER_BUTTON_BACK ) ) want[SDL_SCANCODE_F5]=true;   // quick save
		if( btn( SDL_CONTROLLER_BUTTON_DPAD_UP ) ) want[SDL_SCANCODE_UP]=true;
		if( btn( SDL_CONTROLLER_BUTTON_DPAD_DOWN ) ) want[SDL_SCANCODE_DOWN]=true;
		if( btn( SDL_CONTROLLER_BUTTON_DPAD_LEFT ) ) want[SDL_SCANCODE_LEFT]=true;
		if( btn( SDL_CONTROLLER_BUTTON_DPAD_RIGHT ) ) want[SDL_SCANCODE_RIGHT]=true;

		static const int used[]={ SDL_SCANCODE_A,SDL_SCANCODE_D,SDL_SCANCODE_W,SDL_SCANCODE_S,SDL_SCANCODE_RETURN,
			SDL_SCANCODE_LCTRL,SDL_SCANCODE_TAB,SDL_SCANCODE_SPACE,SDL_SCANCODE_LSHIFT,SDL_SCANCODE_ESCAPE,
			SDL_SCANCODE_F5,SDL_SCANCODE_UP,SDL_SCANCODE_DOWN,SDL_SCANCODE_LEFT,SDL_SCANCODE_RIGHT };
		for( int sc:used ) setKey( sc,want[sc] );
		setButton( 1,lmb );
		setButton( 2,rmb );

		// right stick moves the pointer / looks around
		Uint32 now=SDL_GetTicks();
		float dt=last ? (now-last)/1000.0f : 0.016f;
		last=now;
		if( dt>0.1f ) dt=0.1f;

		float rx=axis( SDL_CONTROLLER_AXIS_RIGHTX ),ry=axis( SDL_CONTROLLER_AXIS_RIGHTY );
		auto shape=[]( float v ){
			const float dead=0.18f;
			float a=fabsf( v );
			if( a<dead ) return 0.0f;
			a=(a-dead)/(1.0f-dead);
			return (v<0 ? -1.0f : 1.0f)*a*a;
		};
		// pixels per second at full deflection (BB_PAD_SPEED overrides; the game's own mouse
		// sensitivity setting scales this further while looking around)
		static float speed=0;
		if( speed==0 ){ const char *e=getenv( "BB_PAD_SPEED" );speed=e ? (float)atof( e ) : 650.0f;if( speed<50 ) speed=650.0f; }
		float dx=shape( rx )*speed*dt+carryX,dy=shape( ry )*speed*dt+carryY;
		int ix=(int)dx,iy=(int)dy;
		carryX=dx-ix;carryY=dy-iy;
		if( ix || iy ){
			px+=ix;py+=iy;
			if( gx_graphics ){
				float w=(float)gx_graphics->getLogicalWidth(),h=(float)gx_graphics->getLogicalHeight();
				px=px<0 ? 0 : (px>w-1 ? w-1 : px);
				py=py<0 ? 0 : (py>h-1 ? h-1 : py);
			}
			deliverMouseMove( (int)px,(int)py );
		}
	}
};

PadEmulation padEmulation;

}

bool SDLRuntime::idle(){
	injectEvents();

	SDL_Event event;
	while( SDL_PollEvent(&event) ){
		if( event.type == SDL_QUIT ){
			RTEX( 0 );
		}else if( event.type==SDL_WINDOWEVENT ){
			if( event.window.event==SDL_WINDOWEVENT_RESIZED ) {
			}
		}else if( event.type==SDL_MOUSEMOTION ){
			padEmulation.pointerMoved( event.motion.x,event.motion.y );
			deliverMouseMove( event.motion.x,event.motion.y );
		}else if( event.type==SDL_MOUSEBUTTONDOWN||event.type==SDL_MOUSEBUTTONUP ){
			int button=0;
			switch( event.button.button ){
			case SDL_BUTTON_LEFT:button=1;break;
			case SDL_BUTTON_MIDDLE:button=3;break;
			case SDL_BUTTON_RIGHT:button=2;break;
			}

			if( button ) deliverMouseButton( button,event.type==SDL_MOUSEBUTTONDOWN );
		}else if( (event.type==SDL_KEYDOWN||event.type==SDL_KEYUP) && event.key.repeat==0 ){
			deliverKey( event.key.keysym.scancode,event.type==SDL_KEYDOWN,false );
		}else if( event.type==SDL_TEXTINPUT||event.type==SDL_TEXTEDITING ){
			// LOGD( "text: %s",event.text.text );
			char *c=event.text.text;
			while( *c ){
				BBEvent ev=BBEvent( BBEVENT_CHAR,*(c++) );
				bbOnEvent.run( &ev );
			}
		}
	}

	// after the queue is drained, so emulated motion lands after the game's own MoveMouse
	padEmulation.pump();

	return true;
}

