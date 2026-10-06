#include "module.h"
#include "ex.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

#ifdef BB_NX
// Set by the Switch stub at startup (a plain pointer rather than a weak symbol, so there is nothing
// for the linker to get wrong); null means "throw", as on every other platform.
extern "C" { void (*bbNxEndHook)()=0; }
#endif

bbEx::bbEx( const char *e ){
	err=e?std::string( e ):"";
	// Debug aid: runtime errors thrown through JIT frames on Windows cannot be
	// caught, so surface them as soon as they are raised.
	if( getenv( "BB_TRACE_ERRORS" ) ) fprintf( stderr,"[bbEx] %s\n",err.c_str() );
#ifdef BB_NX
	// An empty error means "end the program" (End, Stop, close request). It cannot be thrown out
	// of the compiled program on the Switch, so end it here; this does not return.
	if( err.empty() && bbNxEndHook ) bbNxEndHook();
#endif
}

bbEx::~bbEx(){
}
