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
	try{
		if( std::current_exception() ) std::rethrow_exception( std::current_exception() );
	}catch( const std::exception &e ){
		fprintf( stderr,"[fatal] what(): %s\n",e.what() );
	}catch( const char *s ){
		fprintf( stderr,"[fatal] thrown: %s\n",s );
	}catch( ... ){
		fprintf( stderr,"[fatal] thrown: unknown type\n" );
	}
	fflush( stderr );
	abort();
}

// A crash (data abort etc.) is reported to the log: error, registers and a frame-pointer
// backtrace. Addresses are relative to bbStart so they can be looked up in the ELF's symbols.
extern "C" void __libnx_exception_handler( ThreadExceptionDump *ctx ){
	extern int BBCALL bbStart( int,char**,BBMAIN );
	u64 ref=(u64)(void*)&bbStart;
	fprintf( stderr,"[crash] error_desc=0x%x pc=0x%llx lr=0x%llx sp=0x%llx fp=0x%llx far=0x%llx esr=0x%x\n",
		ctx->error_desc,(unsigned long long)ctx->pc.x,(unsigned long long)ctx->lr.x,
		(unsigned long long)ctx->sp.x,(unsigned long long)ctx->fp.x,(unsigned long long)ctx->far.x,ctx->esr );
	fprintf( stderr,"[crash] bbStart=0x%llx (subtract from addresses; pc-bbStart=%lld lr-bbStart=%lld)\n",
		(unsigned long long)ref,(long long)(ctx->pc.x-ref),(long long)(ctx->lr.x-ref) );
	for( int i=0;i<29;i++ ) fprintf( stderr,"[crash] x%d=0x%llx\n",i,(unsigned long long)ctx->cpu_gprs[i].x );
	u64 *fp=(u64*)ctx->fp.x;
	for( int i=0;i<24 && fp && !((u64)fp&7) && (u64)fp>0x1000;i++ ){
		u64 ret=fp[1];
		fprintf( stderr,"[crash] frame %d: ret=0x%llx (bbStart%+lld)\n",i,(unsigned long long)ret,(long long)(ret-ref) );
		u64 *next=(u64*)fp[0];
		if( next<=fp ) break;
		fp=next;
	}
	fflush( stderr );
	svcExitProcess();
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
	setenv( "BB_TRACE_ERRORS","1",1 ); // runtime errors are printed as [bbEx] lines

	std::string cmd_line="";
	bbStartup( argv[0],cmd_line.c_str() );

	bool debug=false;
	StdioDebugger debugger( debug );
	bbAttachDebugger( &debugger );

	return bbruntime_run( bbMain,debug )?0:1;
}
