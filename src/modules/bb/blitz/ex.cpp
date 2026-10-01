#include "module.h"
#include "ex.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

bbEx::bbEx( const char *e ){
	err=e?std::string( e ):"";
	// Debug aid: runtime errors thrown through JIT frames on Windows cannot be
	// caught, so surface them as soon as they are raised.
	if( getenv( "BB_TRACE_ERRORS" ) ) fprintf( stderr,"[bbEx] %s\n",err.c_str() );
}

bbEx::~bbEx(){
}
