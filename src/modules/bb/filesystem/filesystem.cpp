
#include "../stdutil/stdutil.h"
#include "filesystem.h"
#include <bb/stream/stream.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <fstream>
#include <streambuf>
#include <string>
#include <set>

BBFileSystem *gx_filesys;

BBDir::~BBDir(){
}

BBFileSystem::~BBFileSystem(){
}

std::streambuf *BBFileSystem::openFile( const std::string &file,std::ios_base::openmode n ){
	std::filebuf *buf=d_new std::filebuf();
	if( buf->open( file.c_str(),n|std::ios_base::binary ) ){
		return buf;
	}
	delete buf;
	return 0;
}

struct BBFile : public BBStream{
	std::streambuf *buf;
	BBFile( std::streambuf *f ):buf(f){
	}
	~BBFile(){
		delete buf;
	}
	int read( char *buff,int size ){
		return buf->sgetn( (char*)buff,size );
	}
	int write( const char *buff,int size ){
		return buf->sputn( (char*)buff,size );
	}
	int avail(){
		return buf->in_avail();
	}
	int eof(){
		return buf->sgetc()==EOF;
	}
};

static std::set<BBFile*> file_set;

static inline void debugFileSys(){
	if( bb_env.debug ){
		if( !gx_filesys ) RTEX( "Filesystem does not exist" );
	}
}

static inline void debugFile( BBFile *f ){
	if( bb_env.debug ){
		if( !file_set.count( f ) ) RTEX( "File does not exist" );
	}
}

static inline void debugDir( BBDir *d ){
	if( bb_env.debug ){
		if( !gx_filesys->verifyDir( d ) ) RTEX( "Directory does not exist" );
	}
}

#ifdef BB_NX
// Horizon refuses to open a file for reading while it is open for writing elsewhere, and Blitz
// programs routinely call OpenFile (read/write) just to read. Open such files read-only and
// only reopen them read/write on the first write.
class LazyRWBuf : public std::streambuf{
	std::streambuf *fb;
	std::string path;
	bool writable;
	bool upgrade(){
		if( writable ) return true;
		std::streampos pos=fb->pubseekoff( 0,std::ios_base::cur,std::ios_base::in );
		std::streambuf *w=gx_filesys->openFile( path,std::ios_base::in|std::ios_base::out );
		if( !w ) return false;
		delete fb;
		fb=w;writable=true;
		fb->pubseekpos( pos,std::ios_base::in|std::ios_base::out );
		return true;
	}
protected:
	int_type underflow(){ return fb->sgetc(); }
	int_type uflow(){ return fb->sbumpc(); }
	std::streamsize xsgetn( char *s,std::streamsize n ){ return fb->sgetn( s,n ); }
	std::streamsize showmanyc(){ return fb->in_avail(); }
	std::streamsize xsputn( const char *s,std::streamsize n ){ return upgrade() ? fb->sputn( s,n ) : 0; }
	int_type overflow( int_type c ){ return (upgrade() && c!=traits_type::eof()) ? fb->sputc( traits_type::to_char_type( c ) ) : traits_type::eof(); }
	pos_type seekoff( off_type off,std::ios_base::seekdir dir,std::ios_base::openmode which ){ return fb->pubseekoff( off,dir,which ); }
	pos_type seekpos( pos_type pos,std::ios_base::openmode which ){ return fb->pubseekpos( pos,which ); }
	int sync(){ return fb->pubsync(); }
public:
	LazyRWBuf( std::streambuf *b,const std::string &p ):fb( b ),path( p ),writable( false ){}
	~LazyRWBuf(){ delete fb; }
};
#endif

static BBFile *open( BBStr *f,std::ios_base::openmode n ){
	std::string t=canonicalpath( *f );
	std::streambuf *buf;
#ifdef BB_NX
	if( n==(std::ios_base::in|std::ios_base::out) ){
		std::streambuf *r=gx_filesys->openFile( t,std::ios_base::in );
		buf=r ? new LazyRWBuf( r,t ) : 0;
	}else
#endif
	buf=gx_filesys->openFile( t,n );
	if( buf ){
		BBFile *f=d_new BBFile( buf );
		file_set.insert( f );
		return f;
	}
	{
		FILE *probe=fopen( t.c_str(),"rb" );
		fprintf( stderr,"[open failed] '%s' (fopen: %s)\n",t.c_str(),probe ? "works" : strerror( errno ) );
		if( probe ) fclose( probe );
	}
	return 0;
}

BBFile* BBCALL bbReadFile( BBStr *f ){
	return open( f,std::ios_base::in );
}

BBFile* BBCALL bbWriteFile( BBStr *f ){
	return open( f,std::ios_base::out|std::ios_base::trunc );
}

BBFile* BBCALL bbOpenFile( BBStr *f ){
	return open( f,std::ios_base::in|std::ios_base::out );
}

void BBCALL bbCloseFile( BBFile *f ){
	debugFile( f );
	file_set.erase( f );
	delete f;
}

bb_int_t BBCALL bbFilePos( BBFile *f ){
	return f->buf->pubseekoff( 0,std::ios_base::cur );
}

bb_int_t BBCALL bbSeekFile( BBFile *f,bb_int_t pos ){
	return f->buf->pubseekoff( pos,std::ios_base::beg );
}

BBDir* BBCALL bbReadDir( BBStr *d ){
	std::string t=*d;delete d;
	return gx_filesys->openDir( t,0 );
}

void BBCALL bbCloseDir( BBDir *d ){
	gx_filesys->closeDir( d );
}

BBStr* BBCALL bbNextFile( BBDir *d ){
	debugDir( d );
	return d_new BBStr( d->getNextFile() );
}

BBStr* BBCALL bbCurrentDir(){
	debugFileSys();
	return d_new BBStr( gx_filesys->getCurrentDir() );
}

void BBCALL bbChangeDir( BBStr *d ){
	debugFileSys();
	gx_filesys->setCurrentDir( *d );
	delete d;
}

void BBCALL bbCreateDir( BBStr *d ){
	debugFileSys();
	gx_filesys->createDir( *d );
	delete d;
}

void BBCALL bbDeleteDir( BBStr *d ){
	debugFileSys();
	gx_filesys->deleteDir( *d );
	delete d;
}

bb_int_t BBCALL bbFileType( BBStr *f ){
	std::string t=*f;delete f;
	debugFileSys();
	int n=gx_filesys->getFileType( t );
	return n==BBFileSystem::FILE_TYPE_FILE ? 1 : (n==BBFileSystem::FILE_TYPE_DIR ? 2 : 0);
}

bb_int_t BBCALL bbFileSize( BBStr *f ){
	std::string t=*f;delete f;
	debugFileSys();
	return gx_filesys->getFileSize( t );
}

void BBCALL bbCopyFile( BBStr *f,BBStr *to ){
	std::string src=*f,dest=*to;
	delete f;delete to;
	debugFileSys();
	gx_filesys->copyFile( src,dest );
}

void BBCALL bbDeleteFile( BBStr *f ){
	debugFileSys();
	gx_filesys->deleteFile( *f );
	delete f;
}

BBMODULE_CREATE( filesystem ){
	gx_filesys=0;
	return true;
}

BBMODULE_DESTROY( filesystem ){
	if( gx_filesys ){
		while( file_set.size() ) bbCloseFile( *file_set.begin() );

		delete gx_filesys;
		gx_filesys=0;
	}
	return true;
}
