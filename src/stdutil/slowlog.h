#ifndef SLOWLOG_H
#define SLOWLOG_H

// Debug aid for stutter hunting: SLOWLOG("LoadMesh",*name) at the top of a function logs a
// "[slowop]" line to stderr when the call takes longer than 25 ms.
#include <chrono>
#include <cstdio>
#include <string>

struct SlowScope{
	const char *what;
	std::string name;
	std::chrono::steady_clock::time_point start;
	SlowScope( const char *w,const std::string &n ):what( w ),name( n ),start( std::chrono::steady_clock::now() ){}
	~SlowScope(){
		double ms=std::chrono::duration<double,std::milli>( std::chrono::steady_clock::now()-start ).count();
		if( ms>=25 ) fprintf( stderr,"[slowop] %s '%s' %.0f ms\n",what,name.c_str(),ms );
	}
};

#define SLOWLOG( what,name ) SlowScope slowScope_( what,name )

#endif
