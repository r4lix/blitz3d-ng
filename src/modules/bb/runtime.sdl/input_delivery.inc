// --- input delivery ---------------------------------------------------------------
// Real SDL events and the gamepad emulation below both go through these, so the engine
// sees the same thing whichever device produced the input.

extern int bbPointerX,bbPointerY; // graphics: last pointer position the engine was told about

static void deliverKey( int code,bool down,bool synthetic ){
	if( overlayActive ) return; // the overlay menu owns the input
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
	if( overlayActive ) return;
	BBEvent ev( down ? BBEVENT_MOUSEDOWN : BBEVENT_MOUSEUP,button );
	bbOnEvent.run( &ev );
}

static void deliverMouseMove( int x,int y ){
	if( overlayActive ) return;
	bbPointerX=x;bbPointerY=y;
	BBEvent ev( BBEVENT_MOUSEMOVE,0,x,y );
	bbOnEvent.run( &ev );
}

// --- gamepad -> keyboard/mouse ----------------------------------------------------
// SCP: Containment Breach (like most PC games) wants a keyboard and mouse. On the Switch
// the gamepad is translated to the keys the game binds by default (WASD, space to blink,
// shift to sprint, ctrl to crouch, tab for the inventory, F5 to save) and the mouse.
// Enabled on the Switch, or elsewhere with BB_PAD=1.
// SDL's game controller buttons are positional (Xbox layout): the bottom face button is "A". On a
// Switch the bottom button is B and the right one is A, so swap A/B and X/Y to use the labels printed
// on the console everywhere in this file (and in the docs).
static inline int padRemap( int b ){
#ifdef BB_NX
	switch( b ){
	case SDL_CONTROLLER_BUTTON_A:return SDL_CONTROLLER_BUTTON_B;
	case SDL_CONTROLLER_BUTTON_B:return SDL_CONTROLLER_BUTTON_A;
	case SDL_CONTROLLER_BUTTON_X:return SDL_CONTROLLER_BUTTON_Y;
	case SDL_CONTROLLER_BUTTON_Y:return SDL_CONTROLLER_BUTTON_X;
	}
#endif
	return b;
}

namespace{

struct PadEmulation{
	bool initialised=false,enabled=false;
	SDL_GameController *pad=0;
	bool held[SDL_NUM_SCANCODES]={ false };
	bool buttons[4]={ false };
	float px=0,py=0,carryX=0,carryY=0;
	Uint32 last=0;

	// overlay menu trigger: hold "-" (a short press is still the quick save) or hold L3+R3
	bool minusHeld=false,minusConsumed=false;
	Uint32 minusStart=0,chordStart=0;
	int f5Pulse=0;

	// inventory navigation (the game reports slot centres while an inventory is open)
	unsigned slotTickSeen=0;
	int slotQuiet=99;
	bool sticky=false,prevA=false,prevY=false,prevDir[4]={ false,false,false,false };
	int dbl=0;
	Uint32 dirHeldSince[4]={ 0,0,0,0 },dirRepeat[4]={ 0,0,0,0 };

	Uint32 slotSeenAt=0;
	// the game reports the slots once per frame while an inventory is open; idle() runs more often than
	// that (every Delay), so judge by time, not by how often we were called
	bool invUpdate(){
		const Uint32 now=SDL_GetTicks();
		if( bbPadSlotTick!=slotTickSeen ){ slotTickSeen=bbPadSlotTick;slotSeenAt=now; }
		return slotSeenAt && now-slotSeenAt<250 && !bbPadSlots.empty();
	}

	int nearestSlot( float x,float y,float *dist ){
		int best=-1;
		float bd=1e9f;
		for( size_t i=0;i<bbPadSlots.size();i++ ){
			float dx=bbPadSlots[i].first-x,dy=bbPadSlots[i].second-y,d=sqrtf( dx*dx+dy*dy );
			if( d<bd ){ bd=d;best=(int)i; }
		}
		if( dist ) *dist=bd;
		return best;
	}

	// jump to the neighbouring slot in a direction (0 up, 1 down, 2 left, 3 right)
	void moveFocus( int dir ){
		float d=0;
		int cur=nearestSlot( px,py,&d );
		if( cur<0 ) return;
		int target=cur;
		if( d<=60 ){
			const float vx=dir==2 ? -1.0f : dir==3 ? 1.0f : 0.0f,vy=dir==0 ? -1.0f : dir==1 ? 1.0f : 0.0f;
			const float cx=(float)bbPadSlots[cur].first,cy=(float)bbPadSlots[cur].second;
			float bestScore=1e9f;
			for( size_t i=0;i<bbPadSlots.size();i++ ){
				float dx=bbPadSlots[i].first-cx,dy=bbPadSlots[i].second-cy;
				float along=dx*vx+dy*vy,perp=fabsf( dx*vy-dy*vx );
				if( along<=10 ) continue;
				float score=along+2.0f*perp;
				if( score<bestScore ){ bestScore=score;target=(int)i; }
			}
		}
		px=(float)bbPadSlots[target].first;py=(float)bbPadSlots[target].second;
		deliverMouseMove( (int)px,(int)py );
		if( getenv( "BB_TRACE_PAD" ) ) fprintf( stderr,"[pad] focus dir=%d slot %d -> %d at %d,%d (nearest was %.0f px away)\n",dir,cur,target,(int)px,(int)py,d );
	}

	void releaseAll(){
		for( int sc=0;sc<SDL_NUM_SCANCODES;sc++ ) setKey( sc,false );
		for( int b=1;b<4;b++ ) setButton( b,false );
	}

	void fillSnapshot(){
		for( int b=0;b<SDL_CONTROLLER_BUTTON_MAX;b++ ) overlayPad.btn[b]=pad && SDL_GameControllerGetButton( pad,(SDL_GameControllerButton)padRemap( b ) )!=0;
		for( int a=0;a<SDL_CONTROLLER_AXIS_MAX;a++ ) overlayPad.axis[a]=pad ? SDL_GameControllerGetAxis( pad,(SDL_GameControllerAxis)a )/32767.0f : 0.0f;
	}

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
		fillSnapshot();
		if( overlayActive ) return; // the menu reads the snapshot; the game gets nothing
		if( !pad ){
			// nothing connected: release anything we were holding
			releaseAll();
			return;
		}

		const Uint32 tickNow=SDL_GetTicks();
		auto btn=[&]( SDL_GameControllerButton b ){
			return SDL_GameControllerGetButton( pad,(SDL_GameControllerButton)padRemap( b ) )!=0 || (bbInjectPadUntil[b] && tickNow<bbInjectPadUntil[b]);
		};
		auto axis=[&]( SDL_GameControllerAxis a ){ return SDL_GameControllerGetAxis( pad,a )/32767.0f; };

		bool want[SDL_NUM_SCANCODES]={ false };

		// left stick walks (with hysteresis so it does not chatter at the threshold)
		const float lx=axis( SDL_CONTROLLER_AXIS_LEFTX ),ly=axis( SDL_CONTROLLER_AXIS_LEFTY );
		auto threshold=[&]( int sc ){ return held[sc] ? 0.30f : 0.45f; };
		want[SDL_SCANCODE_A]=lx<-threshold( SDL_SCANCODE_A );
		want[SDL_SCANCODE_D]=lx>threshold( SDL_SCANCODE_D );
		want[SDL_SCANCODE_W]=ly<-threshold( SDL_SCANCODE_W );
		want[SDL_SCANCODE_S]=ly>threshold( SDL_SCANCODE_S );

		// inventory open: the stick and d-pad jump between slots, A picks an item up / drops it
		// (press, move, press: no need to hold), Y double-clicks (use / equip)
		const bool inv=invUpdate();
		static bool invWas=false;
		if( inv!=invWas ){ invWas=inv;if( getenv( "BB_TRACE_PAD" ) ) fprintf( stderr,"[pad] inventory mode %s (%d slots, pointer %d,%d)\n",inv ? "on" : "off",(int)bbPadSlots.size(),(int)px,(int)py ); }
		const bool aBtn=btn( SDL_CONTROLLER_BUTTON_A ),yBtn=btn( SDL_CONTROLLER_BUTTON_Y );
		bool lmb;
		if( inv ){
			want[SDL_SCANCODE_A]=want[SDL_SCANCODE_D]=want[SDL_SCANCODE_W]=want[SDL_SCANCODE_S]=false;
			if( aBtn && !prevA ){
				sticky=!sticky;
				if( getenv( "BB_TRACE_PAD" ) ) fprintf( stderr,"[pad] sticky grab %s at %d,%d\n",sticky ? "down" : "up",(int)px,(int)py );
			}
			if( yBtn && !prevY ) dbl=4;
			bool dblDown=dbl==4 || dbl==2;
			if( dbl>0 ) dbl--;
			lmb=sticky || axis( SDL_CONTROLLER_AXIS_TRIGGERRIGHT )>0.4f || dblDown;

			const bool dirNow[4]={ btn( SDL_CONTROLLER_BUTTON_DPAD_UP ) || ly<-0.6f,btn( SDL_CONTROLLER_BUTTON_DPAD_DOWN ) || ly>0.6f,
				btn( SDL_CONTROLLER_BUTTON_DPAD_LEFT ) || lx<-0.6f,btn( SDL_CONTROLLER_BUTTON_DPAD_RIGHT ) || lx>0.6f };
			for( int k=0;k<4;k++ ){
				bool fire=false;
				if( dirNow[k] && !prevDir[k] ){ fire=true;dirHeldSince[k]=tickNow;dirRepeat[k]=tickNow; }
				else if( dirNow[k] && tickNow-dirHeldSince[k]>380 && tickNow-dirRepeat[k]>170 ){ fire=true;dirRepeat[k]=tickNow; }
				prevDir[k]=dirNow[k];
				if( fire ) moveFocus( k );
			}
		}else{
			sticky=false;dbl=0;
			for( int k=0;k<4;k++ ) prevDir[k]=false;
			lmb=aBtn || axis( SDL_CONTROLLER_AXIS_TRIGGERRIGHT )>0.4f;
			if( aBtn ) want[SDL_SCANCODE_RETURN]=true; // "press any key", text boxes
		}
		prevA=aBtn;prevY=yBtn;
		const bool rmb=axis( SDL_CONTROLLER_AXIS_TRIGGERLEFT )>0.4f;
		if( btn( SDL_CONTROLLER_BUTTON_B ) ) want[SDL_SCANCODE_LCTRL]=true;   // crouch
		if( btn( SDL_CONTROLLER_BUTTON_X ) ) want[SDL_SCANCODE_TAB]=true;     // inventory
		if( (!inv && yBtn) || btn( SDL_CONTROLLER_BUTTON_RIGHTSHOULDER ) ) want[SDL_SCANCODE_SPACE]=true; // blink
		if( btn( SDL_CONTROLLER_BUTTON_LEFTSHOULDER ) || btn( SDL_CONTROLLER_BUTTON_LEFTSTICK ) ) want[SDL_SCANCODE_LSHIFT]=true; // sprint
		if( btn( SDL_CONTROLLER_BUTTON_START ) ) want[SDL_SCANCODE_ESCAPE]=true;
		// "-": quick save on a short press; held it opens the overlay menu (or L3+R3 does)
		const Uint32 nowMs=SDL_GetTicks();
		const int trigger=bbSettingInt( "menu_trigger",0 );
		if( trigger==0 ){
			if( btn( SDL_CONTROLLER_BUTTON_BACK ) ){
				if( !minusHeld ){ minusHeld=true;minusStart=nowMs;minusConsumed=false; }
				else if( !minusConsumed && nowMs-minusStart>=1200 ){ minusConsumed=true;overlayRequested=true; }
			}else{
				if( minusHeld && !minusConsumed ) f5Pulse=3;
				minusHeld=false;
			}
			if( f5Pulse>0 ){ want[SDL_SCANCODE_F5]=true;f5Pulse--; }
		}else{
			if( btn( SDL_CONTROLLER_BUTTON_BACK ) ) want[SDL_SCANCODE_F5]=true;
			if( btn( SDL_CONTROLLER_BUTTON_LEFTSTICK ) && btn( SDL_CONTROLLER_BUTTON_RIGHTSTICK ) ){
				if( !chordStart ) chordStart=nowMs;
				else if( nowMs-chordStart>=600 ){ chordStart=0;overlayRequested=true; }
			}else chordStart=0;
		}
		if( !inv ){
			if( btn( SDL_CONTROLLER_BUTTON_DPAD_UP ) ) want[SDL_SCANCODE_UP]=true;
			if( btn( SDL_CONTROLLER_BUTTON_DPAD_DOWN ) ) want[SDL_SCANCODE_DOWN]=true;
			if( btn( SDL_CONTROLLER_BUTTON_DPAD_LEFT ) ) want[SDL_SCANCODE_LEFT]=true;
			if( btn( SDL_CONTROLLER_BUTTON_DPAD_RIGHT ) ) want[SDL_SCANCODE_RIGHT]=true;
		}

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
		const float deadzone=bbSettingFloat( "pad_deadzone",0.18f ),curve=bbSettingFloat( "pad_curve",2.0f );
		auto shape=[deadzone,curve]( float v ){
			float a=fabsf( v );
			if( a<deadzone ) return 0.0f;
			a=(a-deadzone)/(1.0f-deadzone);
			return (v<0 ? -1.0f : 1.0f)*powf( a,curve );
		};
		// pixels per second at full deflection (overlay menu: "Pointer / look speed"; BB_PAD_SPEED
		// overrides; the game's own mouse sensitivity setting scales it further while looking)
		float speed=bbSettingFloat( "pad_speed",650.0f );
		if( const char *e=getenv( "BB_PAD_SPEED" ) ) speed=(float)atof( e );
		if( speed<50 ) speed=650.0f;
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

void overlayReleaseGameInput(){ padEmulation.releaseAll(); }

bool SDLRuntime::idle(){
	injectEvents();

	SDL_Event event;
	while( SDL_PollEvent(&event) ){
		if( event.type == SDL_QUIT ){
			RTEX( 0 );
		}else if( overlayActive ){
			// the overlay menu owns the input: touch/mouse go to it, keys act like pad presses
			if( event.type==SDL_MOUSEMOTION ){
				overlayMouse( event.motion.x,event.motion.y,0,false,true );
			}else if( event.type==SDL_MOUSEBUTTONDOWN||event.type==SDL_MOUSEBUTTONUP ){
				int button=event.button.button==SDL_BUTTON_LEFT ? 1 : event.button.button==SDL_BUTTON_RIGHT ? 2 : 3;
				overlayMouse( event.button.x,event.button.y,button,event.type==SDL_MOUSEBUTTONDOWN,false );
			}else if( event.type==SDL_KEYDOWN && event.key.repeat==0 ){
				switch( event.key.keysym.scancode ){
				case SDL_SCANCODE_UP:overlayPad.pulse[SDL_CONTROLLER_BUTTON_DPAD_UP]=true;break;
				case SDL_SCANCODE_DOWN:overlayPad.pulse[SDL_CONTROLLER_BUTTON_DPAD_DOWN]=true;break;
				case SDL_SCANCODE_LEFT:overlayPad.pulse[SDL_CONTROLLER_BUTTON_DPAD_LEFT]=true;break;
				case SDL_SCANCODE_RIGHT:overlayPad.pulse[SDL_CONTROLLER_BUTTON_DPAD_RIGHT]=true;break;
				case SDL_SCANCODE_RETURN:overlayPad.pulse[SDL_CONTROLLER_BUTTON_A]=true;break;
				case SDL_SCANCODE_ESCAPE:overlayPad.pulse[SDL_CONTROLLER_BUTTON_B]=true;break;
				case SDL_SCANCODE_Q:overlayPad.pulse[SDL_CONTROLLER_BUTTON_LEFTSHOULDER]=true;break;
				case SDL_SCANCODE_E:overlayPad.pulse[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER]=true;break;
				case SDL_SCANCODE_F10:overlayPad.pulse[SDL_CONTROLLER_BUTTON_START]=true;break;
				default:break;
				}
			}
		}else if( event.type==SDL_WINDOWEVENT ){
			if( event.window.event==SDL_WINDOWEVENT_RESIZED ) {
			}
		}else if( event.type==SDL_KEYDOWN && event.key.keysym.scancode==SDL_SCANCODE_F10 && event.key.repeat==0 ){
			overlayRequested=true; // keyboards (PC) open the menu with F10
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

