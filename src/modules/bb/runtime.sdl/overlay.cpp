// In-game settings overlay: Dear ImGui over a frozen copy of the last game frame.
// See overlay.h. The menu is modal: the game is not running while it is open.

#include "runtime.sdl.h"
#include "overlay.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_opengl3.h"
#include "../stdutil/stdutil.h"
#include <bb/audio/driver.h>
#include <bb/graphics/graphics.h>
#include <bb/runtime/runtime.h>
#include <bb/blitz/blitz.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#ifdef BB_NX
#include <malloc.h>
#endif

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

extern int bbPausedMs;
bool bbSaveBackBuffer( const std::string &path );

OverlayPad overlayPad;
bool overlayRequested=false;
bool overlayActive=false;

namespace overlay_impl{

bool inited=false,hasFont=false;
GLuint bgTex=0;
int dispW=1280,dispH=720;

// ---- input ----------------------------------------------------------------------
bool prevDown[SDL_CONTROLLER_BUTTON_MAX]={ false };
float holdTime[SDL_CONTROLLER_BUTTON_MAX]={ 0 };
bool pressedNow[SDL_CONTROLLER_BUTTON_MAX]={ false };
bool armed=false;          // ignore the buttons that opened the menu until they are released
bool mouseInput=false;     // the last input came from touch/mouse

bool isDown( int b ){ return overlayPad.btn[b] || overlayPad.pulse[b]; }

// press edge, with auto repeat for d-pad style navigation
void updateButtons( float dt ){
	for( int b=0;b<SDL_CONTROLLER_BUTTON_MAX;b++ ){
		bool d=isDown( b );
		pressedNow[b]=false;
		if( d && !prevDown[b] ){ pressedNow[b]=true;holdTime[b]=0; }
		else if( d ){
			float before=holdTime[b];
			holdTime[b]+=dt;
			if( holdTime[b]>0.45f && (int)((holdTime[b]-0.45f)/0.08f)>(int)((before-0.45f)/0.08f) ) pressedNow[b]=true;
		}else holdTime[b]=0;
		prevDown[b]=d;
	}
}
bool pressed( int b ){ return armed && pressedNow[b]; }

// ---- settings -------------------------------------------------------------------
const char *kPadSpeed="pad_speed",*kDeadzone="pad_deadzone",*kCurve="pad_curve",*kTrigger="menu_trigger";
const char *kFont="font_scale",*kTexMax="texture_max",*kShowFps="show_fps",*kVolume="master_volume";

#ifdef BB_NX
const int kDefaultTexMax=512;
#else
const int kDefaultTexMax=0;
#endif

std::string fmtF( float v,int prec ){
	char b[32];
	snprintf( b,sizeof(b),"%.*f",prec,v );
	return b;
}

void setF( const char *key,float v,int prec=2 ){ bbSettingSet( key,fmtF( v,prec ) ); }
void setI( const char *key,int v ){ bbSettingSet( key,std::to_string( v ) ); }

// options.ini (the game's own file): the render resolution lives there
std::string readOptions(){
	std::string out;
	if( FILE *f=fopen( "options.ini","rb" ) ){
		char buf[4096];
		size_t n;
		while( (n=fread( buf,1,sizeof(buf),f ))>0 ) out.append( buf,n );
		fclose( f );
	}
	return out;
}

bool optionValue( const std::string &text,const char *key,int *value ){
	size_t pos=0;
	while( pos<text.size() ){
		size_t end=text.find( '\n',pos );
		if( end==std::string::npos ) end=text.size();
		std::string line=text.substr( pos,end-pos );
		pos=end+1;
		if( !line.empty() && line[0]=='[' && pos>end+1 && line!="[options]\r" && line!="[options]" ) break; // left [options]
		size_t eq=line.find( '=' );
		if( eq==std::string::npos ) continue;
		std::string k=line.substr( 0,eq );
		while( !k.empty() && (k.back()==' ' || k.back()=='\t') ) k.pop_back();
		if( strcasecmp( k.c_str(),key )==0 ){ *value=atoi( line.c_str()+eq+1 );return true; }
	}
	return false;
}

void writeResolution( int w,int h ){
	std::string text=readOptions();
	if( text.empty() ) return;
	std::string out;
	bool inOptions=false,doneW=false,doneH=false;
	size_t pos=0;
	while( pos<text.size() ){
		size_t end=text.find( '\n',pos );
		bool last=end==std::string::npos;
		if( last ) end=text.size();
		std::string line=text.substr( pos,end-pos );
		std::string eol=last ? "" : "\n";
		pos=end+1;
		std::string trimmed=line;
		while( !trimmed.empty() && (trimmed.back()=='\r' || trimmed.back()==' ') ) trimmed.pop_back();
		if( !trimmed.empty() && trimmed[0]=='[' ) inOptions=strcasecmp( trimmed.c_str(),"[options]" )==0;
		size_t eq=line.find( '=' );
		if( inOptions && eq!=std::string::npos ){
			std::string k=line.substr( 0,eq );
			while( !k.empty() && (k.back()==' ' || k.back()=='\t') ) k.pop_back();
			bool cr=!line.empty() && line.back()=='\r';
			if( !doneW && strcasecmp( k.c_str(),"width" )==0 ){ line="width = "+std::to_string( w )+(cr ? "\r" : "");doneW=true; }
			else if( !doneH && strcasecmp( k.c_str(),"height" )==0 ){ line="height = "+std::to_string( h )+(cr ? "\r" : "");doneH=true; }
		}
		out+=line+eol;
	}
	if( FILE *f=fopen( "options.ini","wb" ) ){
		fwrite( out.data(),1,out.size(),f );
		fclose( f );
	}
}

struct Res{ int w,h; };
const Res kRes[]={ {1280,720},{1152,648},{1024,576},{960,540},{854,480} };
const int kResCount=sizeof(kRes)/sizeof(kRes[0]);

// ---- widgets --------------------------------------------------------------------
int focusRow=0,rowCount=0,curRow=0;
bool scrollToFocus=false;
bool confirmQuit=false;

// One settings row: "label      <  value  >". Returns -1/+1 for left/right (or touch on the
// left/right side), 2 for an activation (A button or a tap in the middle), else 0.
int row( const char *label,const std::string &value,bool arrows=true ){
	int idx=curRow++;
	ImGui::PushID( idx );
	ImVec2 p=ImGui::GetCursorScreenPos();
	float w=ImGui::GetContentRegionAvail().x,h=ImGui::GetTextLineHeight()+16.0f;
	bool focused=idx==focusRow;
	ImGui::Selectable( "##row",focused,0,ImVec2( w,h ) );
	bool tapped=ImGui::IsItemClicked( 0 ) && mouseInput;
	if( ImGui::IsItemHovered() && mouseInput && ImGui::IsMouseClicked( 0 ) ) focusRow=idx;

	ImDrawList *dl=ImGui::GetWindowDrawList();
	ImU32 col=ImGui::GetColorU32( focused ? ImVec4( 1,1,1,1 ) : ImVec4( 0.78f,0.78f,0.78f,1 ) );
	dl->AddText( ImVec2( p.x+12,p.y+8 ),col,label );
	std::string v=arrows ? "<  "+value+"  >" : value;
	ImVec2 ts=ImGui::CalcTextSize( v.c_str() );
	dl->AddText( ImVec2( p.x+w-ts.x-14,p.y+8 ),col,v.c_str() );
	if( focused && scrollToFocus ) ImGui::SetScrollHereY( 0.5f );

	int result=0;
	if( tapped ){
		float mx=ImGui::GetIO().MousePos.x-p.x;
		float valueStart=w-ts.x-14,mid=valueStart+ts.x*0.5f;
		if( arrows && mx>valueStart-30 && mx<mid ) result=-1;
		else if( arrows && mx>=mid ) result=1;
		else result=2;
	}else if( focused ){
		if( pressed( SDL_CONTROLLER_BUTTON_DPAD_LEFT ) ) result=-1;
		else if( pressed( SDL_CONTROLLER_BUTTON_DPAD_RIGHT ) ) result=1;
		else if( pressed( SDL_CONTROLLER_BUTTON_A ) ) result=2;
	}
	ImGui::PopID();
	return result;
}

bool floatRow( const char *label,const char *key,float def,float lo,float hi,float step,int prec,const char *suffix="",float mul=1.0f ){
	float v=bbSettingFloat( key,def );
	int r=row( label,fmtF( v*mul,prec )+suffix );
	if( r==0 ) return false;
	if( r==2 ) r=1;
	v+=r*step;
	if( v<lo-1e-4f ) v=lo; // stop at the ends
	if( v>hi+1e-4f ) v=hi;
	setF( key,v,prec+(mul<1 ? 2 : 0) );
	return true;
}

bool boolRow( const char *label,const char *key,bool def,const char *on="On",const char *off="Off" ){
	bool v=bbSettingInt( key,def ? 1 : 0 )!=0;
	int r=row( label,v ? on : off );
	if( r==0 ) return false;
	setI( key,v ? 0 : 1 );
	return true;
}

void sectionNote( const char *text ){
	ImGui::PushStyleColor( ImGuiCol_Text,ImVec4( 0.6f,0.6f,0.6f,1 ) );
	ImGui::TextWrapped( "%s",text );
	ImGui::PopStyleColor();
}

// ---- tabs -----------------------------------------------------------------------
int tab=0;
const char *kTabs[]={ "Controls","Display","Audio","System" };
bool closeRequested=false,quitRequested=false;

void applyAudio(){
	if( gx_audio ) gx_audio->setVolume( bbSettingInt( kVolume,100 )/100.0f );
}

void resetDefaults(){
	const char *keys[]={ kPadSpeed,kDeadzone,kCurve,kTrigger,kFont,kTexMax,kShowFps,kVolume };
	for( const char *k:keys ) bbSettingSet( k,"" );
	// an empty value reads as "not set": remove by writing the defaults explicitly
	setF( kPadSpeed,650,0 );setF( kDeadzone,0.18f,2 );setF( kCurve,2.0f,2 );setI( kTrigger,0 );
	setF( kFont,1.15f,2 );setI( kTexMax,kDefaultTexMax );setI( kShowFps,0 );setI( kVolume,100 );
	writeResolution( 1280,720 );
	applyAudio();
}

void drawControls(){
	floatRow( "Pointer / look speed",kPadSpeed,650,150,1500,50,0," px/s" );
	floatRow( "Stick dead zone",kDeadzone,0.18f,0.05f,0.40f,0.01f,0,"%",100.0f );
	floatRow( "Stick response curve",kCurve,2.0f,1.0f,3.0f,0.25f,2,"" );
	{
		int t=bbSettingInt( kTrigger,0 );
		int r=row( "Open this menu with",t==0 ? "Hold  -  (1.2 s)" : "Hold  L3 + R3" );
		if( r ) setI( kTrigger,t==0 ? 1 : 0 );
	}
	sectionNote( "Response curve 1.0 is linear, 2.0 gives fine control near the centre. "
		"With L3+R3 the - button is a plain quick save." );
}

void drawDisplay(){
	{
		std::string text=readOptions();
		int w=1280,h=720;
		optionValue( text,"width",&w );
		optionValue( text,"height",&h );
		int cur=-1;
		for( int i=0;i<kResCount;i++ ) if( kRes[i].w==w && kRes[i].h==h ) cur=i;
		std::string label=std::to_string( w )+" x "+std::to_string( h );
		int r=row( "Render resolution (restart)",label );
		if( r ){
			int n=cur<0 ? 0 : cur+(r==-1 ? -1 : 1);
			if( n<0 ) n=0;
			if( n>=kResCount ) n=kResCount-1;
			writeResolution( kRes[n].w,kRes[n].h );
		}
	}
	{
		int cap=bbSettingInt( kTexMax,kDefaultTexMax );
		int r=row( "Texture size limit (restart)",cap<=0 ? "Original" : std::to_string( cap )+" px" );
		if( r ){
			const int opts[]={ 256,512,1024,0 };
			int i=0;
			for( int k=0;k<4;k++ ) if( opts[k]==(cap<0 ? 0 : cap) ) i=k;
			i+=(r==-1 ? -1 : 1);
			if( i<0 ) i=0;
			if( i>3 ) i=3;
			setI( kTexMax,opts[i] );
		}
	}
	floatRow( "Text size (restart)",kFont,1.15f,0.8f,1.8f,0.05f,2,"x" );
	boolRow( "FPS counter",kShowFps,false );
	sectionNote( "Lower resolutions and smaller textures use less memory and run faster. "
		"Items marked (restart) apply the next time the game starts." );
}

void drawAudio(){
	float v=(float)bbSettingInt( kVolume,100 );
	int r=row( "Master volume",std::to_string( (int)v )+"%" );
	if( r ){
		v+=(r==-1 ? -5 : 5);
		if( v<0 ) v=0;
		if( v>100 ) v=100;
		setI( kVolume,(int)v );
		applyAudio();
	}
	sectionNote( "Scales everything on top of the game's own volume sliders." );
}

void drawSystem(){
	if( row( "Resume game","",false )==2 ) closeRequested=true;
	if( row( "Reset all settings",confirmQuit ? "" : "",false )==2 ) resetDefaults();
	{
		int r=row( "Quit game",confirmQuit ? "Press A again to confirm" : "",false );
		if( r==2 ){
			if( confirmQuit ) quitRequested=true,closeRequested=true;
			else confirmQuit=true;
		}else if( r!=0 || pressed( SDL_CONTROLLER_BUTTON_DPAD_UP ) || pressed( SDL_CONTROLLER_BUTTON_DPAD_DOWN ) ) confirmQuit=false;
	}
	ImGui::Spacing();
	const GLubyte *renderer=glGetString( GL_RENDERER ),*version=glGetString( GL_VERSION );
	ImGui::TextDisabled( "GPU:     %s",renderer ? (const char*)renderer : "?" );
	ImGui::TextDisabled( "GL:      %s",version ? (const char*)version : "?" );
	ImGui::TextDisabled( "Display: %d x %d",dispW,dispH );
#ifdef BB_NX
	struct mallinfo mi=mallinfo();
	ImGui::TextDisabled( "Heap:    %d MB in use",(int)(((unsigned)mi.uordblks)>>20) );
#endif
	ImGui::TextDisabled( "Settings file: switch_settings.ini" );
}

void drawMenu(){
	ImGuiIO &io=ImGui::GetIO();
	const float W=io.DisplaySize.x,H=io.DisplaySize.y;
	ImGui::SetNextWindowPos( ImVec2( W*0.5f,H*0.5f ),ImGuiCond_Always,ImVec2( 0.5f,0.5f ) );
	ImGui::SetNextWindowSize( ImVec2( W*0.82f,H*0.86f ) );
	ImGui::Begin( "##overlay",0,ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|
		ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoSavedSettings );

	// tab strip (touch: tap a tab, pad: L / R)
	ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing,ImVec2( 8,8 ) );
	float tabW=(ImGui::GetContentRegionAvail().x-8*3)/4;
	for( int i=0;i<4;i++ ){
		if( i ) ImGui::SameLine();
		ImGui::PushStyleColor( ImGuiCol_Button,i==tab ? ImVec4( 0.62f,0.12f,0.12f,1 ) : ImVec4( 0.16f,0.16f,0.16f,1 ) );
		ImGui::PushStyleColor( ImGuiCol_ButtonHovered,i==tab ? ImVec4( 0.7f,0.15f,0.15f,1 ) : ImVec4( 0.22f,0.22f,0.22f,1 ) );
		ImGui::PushStyleColor( ImGuiCol_ButtonActive,ImVec4( 0.7f,0.15f,0.15f,1 ) );
		ImGui::PushItemFlag( ImGuiItemFlags_NoNav,true );
		if( ImGui::Button( kTabs[i],ImVec2( tabW,0 ) ) ){ tab=i;focusRow=0;scrollToFocus=true; }
		ImGui::PopItemFlag();
		ImGui::PopStyleColor( 3 );
	}
	ImGui::PopStyleVar();
	ImGui::Separator();

	// inputs that change the tab / focus
	if( pressed( SDL_CONTROLLER_BUTTON_LEFTSHOULDER ) ){ tab=(tab+3)%4;focusRow=0;scrollToFocus=true;confirmQuit=false; }
	if( pressed( SDL_CONTROLLER_BUTTON_RIGHTSHOULDER ) ){ tab=(tab+1)%4;focusRow=0;scrollToFocus=true;confirmQuit=false; }
	int before=focusRow;
	if( pressed( SDL_CONTROLLER_BUTTON_DPAD_UP ) ) focusRow--;
	if( pressed( SDL_CONTROLLER_BUTTON_DPAD_DOWN ) ) focusRow++;
	if( rowCount>0 ){
		if( focusRow<0 ) focusRow=0;
		if( focusRow>=rowCount ) focusRow=rowCount-1;
	}
	if( focusRow!=before ) scrollToFocus=true;

	ImGui::BeginChild( "##content",ImVec2( 0,-ImGui::GetTextLineHeightWithSpacing()-14 ),false,ImGuiWindowFlags_NoNav );
	curRow=0;
	switch( tab ){
	case 0:drawControls();break;
	case 1:drawDisplay();break;
	case 2:drawAudio();break;
	case 3:drawSystem();break;
	}
	rowCount=curRow;
	ImGui::EndChild();
	scrollToFocus=false;

	ImGui::Separator();
	ImGui::TextDisabled( "D-pad: select / change    A: toggle    L / R: tab    B or +: close" );
	ImGui::End();

	if( pressed( SDL_CONTROLLER_BUTTON_B ) || pressed( SDL_CONTROLLER_BUTTON_START ) || pressed( SDL_CONTROLLER_BUTTON_BACK ) ) closeRequested=true;
}

// ---- gl -------------------------------------------------------------------------
bool ensureInit(){
	if( inited ) return true;
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io=ImGui::GetIO();
	io.IniFilename=0;
	io.LogFilename=0;
	io.ConfigFlags|=ImGuiConfigFlags_NoMouseCursorChange;

	const float size=21.0f;
	ImFont *f=0;
	if( FILE *probe=fopen( "GFX/font/courbd/Courier New.ttf","rb" ) ){
		fclose( probe );
		f=io.Fonts->AddFontFromFileTTF( "GFX/font/courbd/Courier New.ttf",size );
	}
	if( !f ){
		f=io.Fonts->AddFontDefault();
		io.FontGlobalScale=1.6f;
	}
	hasFont=f!=0;

	ImGui::StyleColorsDark();
	ImGuiStyle &st=ImGui::GetStyle();
	st.WindowRounding=6;st.FrameRounding=4;st.WindowBorderSize=1;
	st.Colors[ImGuiCol_WindowBg]=ImVec4( 0.05f,0.05f,0.05f,0.94f );
	st.Colors[ImGuiCol_Header]=ImVec4( 0.62f,0.12f,0.12f,0.80f );
	st.Colors[ImGuiCol_HeaderHovered]=ImVec4( 0.62f,0.12f,0.12f,0.50f );
	st.Colors[ImGuiCol_HeaderActive]=ImVec4( 0.70f,0.15f,0.15f,0.90f );
	st.Colors[ImGuiCol_Border]=ImVec4( 0.55f,0.55f,0.55f,0.60f );

#ifdef BB_NX
	const char *glsl="#version 300 es";
#else
	const char *glsl="#version 330 core";
#endif
	if( !ImGui_ImplOpenGL3_Init( glsl ) ){
		fprintf( stderr,"[overlay] ImGui GL init failed\n" );
		ImGui::DestroyContext();
		return false;
	}
	inited=true;
	fprintf( stderr,"[overlay] ready (%s)\n",glsl );
	return true;
}

void beginFrame( float dt ){
	ImGuiIO &io=ImGui::GetIO();
	if( gx_graphics ){
		dispW=gx_graphics->getWidth();
		dispH=gx_graphics->getHeight();
	}
	io.DisplaySize=ImVec2( (float)dispW,(float)dispH );
	io.DeltaTime=dt>0.0001f ? dt : 0.0001f;
	ImGui_ImplOpenGL3_NewFrame();
	ImGui::NewFrame();
}

void captureBackground(){
	GLint prevTex=0,prevFb=0,prevActive=0;
	glGetIntegerv( GL_ACTIVE_TEXTURE,&prevActive );
	glActiveTexture( GL_TEXTURE0 );
	glGetIntegerv( GL_TEXTURE_BINDING_2D,&prevTex );
	glGetIntegerv( GL_FRAMEBUFFER_BINDING,&prevFb );
	glBindFramebuffer( GL_FRAMEBUFFER,0 );
	if( !bgTex ) glGenTextures( 1,&bgTex );
	glBindTexture( GL_TEXTURE_2D,bgTex );
	int w=gx_graphics ? gx_graphics->getWidth() : 1280,h=gx_graphics ? gx_graphics->getHeight() : 720;
	glReadBuffer( GL_BACK );
	glCopyTexImage2D( GL_TEXTURE_2D,0,GL_RGBA,0,0,w,h,0 );
	glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D,(GLuint)prevTex );
	glBindFramebuffer( GL_FRAMEBUFFER,(GLuint)prevFb );
	glActiveTexture( (GLenum)prevActive );
}

void drawBackground( float dim ){
	ImDrawList *dl=ImGui::GetBackgroundDrawList();
	dl->AddImage( (ImTextureID)(intptr_t)bgTex,ImVec2( 0,0 ),ImVec2( (float)dispW,(float)dispH ),ImVec2( 0,1 ),ImVec2( 1,0 ) );
	if( dim>0 ) dl->AddRectFilled( ImVec2( 0,0 ),ImVec2( (float)dispW,(float)dispH ),IM_COL32( 0,0,0,(int)(dim*255) ) );
}

void presentFrame( bool swap ){
	ImGui::Render();
	GLint prevFb=0;
	GLfloat cc[4];
	glGetIntegerv( GL_FRAMEBUFFER_BINDING,&prevFb );
	glGetFloatv( GL_COLOR_CLEAR_VALUE,cc );
	glBindFramebuffer( GL_FRAMEBUFFER,0 );
	if( swap ){
		glDisable( GL_SCISSOR_TEST );
		glViewport( 0,0,dispW,dispH );
		glClearColor( 0,0,0,1 );
		glClear( GL_COLOR_BUFFER_BIT );
	}
	ImGui_ImplOpenGL3_RenderDrawData( ImGui::GetDrawData() );
	if( swap ){
		// BB_OVERLAY_SHOTS=<prefix>: save every 25th menu frame (testing without a screen)
		static int frame=0,shots=0;
		static const char *prefix=getenv( "BB_OVERLAY_SHOTS" );
		if( prefix && ++frame%90==0 && shots<14 ){
			bbSaveBackBuffer( std::string( prefix )+"_"+std::to_string( shots++ )+".bmp" );
		}
		bbContextDriver->flip( true );
	}
	glClearColor( cc[0],cc[1],cc[2],cc[3] );
	glBindFramebuffer( GL_FRAMEBUFFER,(GLuint)prevFb );
}

// FPS counter shown over the game
void drawHud(){
	using clk=std::chrono::steady_clock;
	static clk::time_point last=clk::now(),windowStart=clk::now();
	static int frames=0;
	static float fps=0,worst=0;
	auto now=clk::now();
	float ms=std::chrono::duration<float,std::milli>( now-last ).count();
	last=now;
	frames++;
	if( ms>worst ) worst=ms;
	float w=std::chrono::duration<float,std::milli>( now-windowStart ).count();
	if( w>=500 ){ fps=frames*1000.0f/w;frames=0;windowStart=now;worst=0; }

	if( !ensureInit() ) return;
	beginFrame( ms/1000.0f );
	char text[64];
	snprintf( text,sizeof(text),"%.0f FPS  %.1f ms",fps,ms );
	ImDrawList *dl=ImGui::GetForegroundDrawList();
	ImVec2 ts=ImGui::CalcTextSize( text );
	dl->AddRectFilled( ImVec2( 8,8 ),ImVec2( 20+ts.x,16+ts.y ),IM_COL32( 0,0,0,150 ),4 );
	dl->AddText( ImVec2( 14,12 ),IM_COL32( 255,255,255,255 ),text );
	presentFrame( false );
}

} // namespace overlay_impl

using namespace overlay_impl;

void overlayMouse( int x,int y,int button,bool down,bool motion ){
	if( !inited ) return;
	mouseInput=true;
	ImGuiIO &io=ImGui::GetIO();
	io.AddMousePosEvent( (float)x,(float)y );
	if( !motion && button>=1 && button<=3 ) io.AddMouseButtonEvent( button==1 ? 0 : button==2 ? 1 : 2,down );
}

static void runModal(){
	if( !ensureInit() ){ overlayRequested=false;return; }

	overlayReleaseGameInput();
	overlayActive=true;
	overlayRequested=false;
	closeRequested=quitRequested=false;
	confirmQuit=false;
	armed=false;
	mouseInput=false;
	memset( prevDown,0,sizeof(prevDown) );
	memset( holdTime,0,sizeof(holdTime) );
	focusRow=0;scrollToFocus=true;

	captureBackground();
	Uint32 t0=SDL_GetTicks(),last=t0;
	if( gx_audio ) gx_audio->setPaused( true );

	ImGuiIO &io=ImGui::GetIO();
	io.AddMouseButtonEvent( 0,false );

	while( !closeRequested ){
		if( !bbRuntimeIdle() ){ quitRequested=true;break; }

		Uint32 now=SDL_GetTicks();
		float dt=(now-last)/1000.0f;
		last=now;

		updateButtons( dt );
		if( !armed ){
			bool any=false;
			for( int b=0;b<SDL_CONTROLLER_BUTTON_MAX;b++ ) if( overlayPad.btn[b] ) any=true;
			if( !any ) armed=true;
		}
		// the keyboard pulses last one frame
		bool pulsed[SDL_CONTROLLER_BUTTON_MAX];
		memcpy( pulsed,overlayPad.pulse,sizeof(pulsed) );
		for( int b=0;b<SDL_CONTROLLER_BUTTON_MAX;b++ ) if( pressedNow[b] && overlayPad.btn[b] ) mouseInput=false;

		beginFrame( dt );
		drawBackground( 0.55f );
		drawMenu();
		presentFrame( true );

		memset( overlayPad.pulse,0,sizeof(overlayPad.pulse) );
		(void)pulsed;
	}

	// leave the game's frame on screen: bbFlip presents the back buffer right after we return
	beginFrame( 0.016f );
	drawBackground( 0 );
	presentFrame( true );

	overlayActive=false;
	if( gx_audio ) gx_audio->setPaused( false );
	bbPausedMs+=(int)(SDL_GetTicks()-t0);
	bbSettingsSave();
	applyAudio();
	fprintf( stderr,"[overlay] closed after %u ms\n",(unsigned)(SDL_GetTicks()-t0) );
	if( quitRequested ) RTEX( 0 );
}

void overlayFrameHook(){
	static bool first=true;
	if( first ){
		first=false;
		applyAudio();
	}
	if( overlayRequested ){ runModal();return; }
	if( bbSettingInt( kShowFps,0 ) ) drawHud();
}
