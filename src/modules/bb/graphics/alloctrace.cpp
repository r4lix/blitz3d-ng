// Debug aid (Windows): BB_TRACE_ALLOC=<bytes> makes the runtime print a call stack for
// every 20th operator new of at least that size, which finds what is leaking memory.
// Without the variable the replacement operator new is a plain malloc.

#ifdef WIN32
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace{

size_t threshold=0;
bool initialised=false;
long counter=0;
thread_local bool inTrace=false;

void report( size_t n ){
	if( inTrace ) return;
	inTrace=true;
	// BB_TRACE_ALLOC_EVERY=1 logs every allocation (default: every 20th)
	static long every=0;
	if( !every ){ const char *e=getenv( "BB_TRACE_ALLOC_EVERY" );every=e ? atol( e ) : 20;if( every<1 ) every=1; }
	if( (++counter)%every==0 ){
		void *frames[20];
		USHORT count=CaptureStackBackTrace( 2,20,frames,0 );
		HANDLE proc=GetCurrentProcess();
		static bool symInit=false;
		if( !symInit ){
			SymSetOptions( SYMOPT_DEFERRED_LOADS|SYMOPT_UNDNAME );
			SymInitialize( proc,0,TRUE );
			symInit=true;
		}
		fprintf( stderr,"[alloc] %zu bytes (#%ld):",n,counter );
		for( USHORT i=0;i<count&&i<6;i++ ){
			char buf[sizeof(SYMBOL_INFO)+200];
			SYMBOL_INFO *sym=(SYMBOL_INFO*)buf;
			memset( buf,0,sizeof(buf) );
			sym->SizeOfStruct=sizeof(SYMBOL_INFO);sym->MaxNameLen=199;
			DWORD64 disp=0;
			if( SymFromAddr( proc,(DWORD64)frames[i],&disp,sym ) ) fprintf( stderr," %s",sym->Name );
			else fprintf( stderr," ?" );
		}
		fprintf( stderr,"\n" );
		fflush( stderr );
	}
	inTrace=false;
}

void *allocate( size_t n ){
	if( !initialised ){
		initialised=true;
		if( const char *t=getenv( "BB_TRACE_ALLOC" ) ) threshold=(size_t)atoll( t );
	}
	void *p=malloc( n ? n : 1 );
	if( !p ) throw std::bad_alloc();
	if( threshold && n>=threshold ) report( n );
	return p;
}

}

void *operator new( size_t n ){ return allocate( n ); }
void *operator new[]( size_t n ){ return allocate( n ); }
void operator delete( void *p ) noexcept { free( p ); }
void operator delete[]( void *p ) noexcept { free( p ); }
void operator delete( void *p,size_t ) noexcept { free( p ); }
void operator delete[]( void *p,size_t ) noexcept { free( p ); }
#endif
