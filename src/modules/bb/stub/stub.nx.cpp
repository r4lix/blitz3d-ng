#include <bb/stub/stub.h>
#include <bb/runtime/runtime.h>

#include <iostream>

#include <stdio.h>
#include <unistd.h>
#include <exception>
#include <stdlib.h>
#include <string.h>

#include <switch.h>

class StdioDebugger : public Debugger{
private:
	bool trace;
	std::string file;
	int row,col;
public:
	StdioDebugger( bool t ):trace(t){
	}

	void debugRun(){
	}
	void debugStop(){
	}
	void debugStmt( int srcpos,const char *f ){
		file=std::string(f);
		row=(srcpos>>16)&0xffff;
		col=srcpos&0xffff;
		if( trace ) std::cout<<file<<":"<<"["<<row+1<<":"<<col<<"]"<<std::endl;
	}
	void debugEnter( void *frame,void *env,const char *func ){
	}
	void debugLeave(){
	}
	void debugLog( const char *msg ){
		std::cout<<file<<":"<<"["<<row+1<<":"<<col<<"] "<<msg<<std::endl;
	}
	void debugMsg( const char *msg,bool serious ){
		if( serious ){
			std::cout<<file<<":"<<"["<<row+1<<":"<<col<<"] "<<msg<<std::endl;
		}
	}
	void debugSys( void *msg ){
		std::cout<<file<<":"<<"["<<row+1<<":"<<col<<"] "<<msg<<std::endl;
	}
};

static void logTerminate(){
	fprintf( stderr,"[fatal] std::terminate (uncaught C++ exception)\n" );
	fflush( stderr );
	abort();
}

extern "C"
int BBCALL bbStart( int argc,char *argv[], BBMAIN bbMain ) {
	// Programs started from hbmenu are given their own path ("sdmc:/switch/game/game.nro");
	// the game's files are expected next to it.
	std::string exe=(argc>0 && argv && argv[0]) ? argv[0] : "";
	std::string dir="sdmc:/switch/scpcb/";
	size_t slash=exe.find_last_of( '/' );
	if( slash!=std::string::npos ) dir=exe.substr( 0,slash+1 );
	chdir( dir.c_str() );

	// There is no console to read, so keep a log on the SD card (flushed on every write).
	freopen( "scpcb.log","w",stderr );
	freopen( "scpcb.log","a",stdout );
	setvbuf( stderr,0,_IONBF,0 );
	setvbuf( stdout,0,_IONBF,0 );
	fprintf( stderr,"start: argv0=%s cwd=%s\n",exe.c_str(),dir.c_str() );
	std::set_terminate( logTerminate );

	std::string cmd_line="";
	bbStartup( argv[0],cmd_line.c_str() );

	bool debug=false;
	StdioDebugger debugger( debug );
	bbAttachDebugger( &debugger );

	return bbruntime_run( bbMain,debug )?0:1;
}
