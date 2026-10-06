#include "overlay.nx.h"

#include <switch.h>
#include <sys/iosupport.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <set>
#include <string>

// See overlay.nx.h. Both layers are existing devoptabs ("save"/"sdmc" above, "romfs" below);
// every call is forwarded to them with a fully qualified path, so neither one's own working
// directory is involved.

namespace{

const devoptab_t *g_upper=0,*g_lower=0;
std::string g_upperRoot;	// "save:" or "sdmc:/switch/scpcb"
std::string g_cwd="/";		// always starts and ends with '/'
bool g_commit=false;		// the upper layer is save data and needs fsdevCommitDevice
std::string g_status="overlay not initialised";


// libnx devices (romfs, fsdev) find their mount through r->deviceData, which the C library sets to
// the called device's data before each call. Forwarding to another device means doing that ourselves,
// otherwise the callee sees our (null) deviceData and dereferences a null mount.
struct DevScope{
	struct _reent *r;
	void *saved;
	DevScope( struct _reent *r_,const devoptab_t *d ):r(r_),saved(r_->deviceData){ r->deviceData=d->deviceData; }
	~DevScope(){ r->deviceData=saved; }
};
#define CALL( dev,fn,r,... ) ([&]{ DevScope scope_( r,dev );return (dev)->fn( r,__VA_ARGS__ ); }())

struct OvFile{
	const devoptab_t *dev;	// layer the file was opened on
	int wrote;
	alignas(8) char inner[1];	// that layer's own file struct
};

struct OvDir{
	DIR_ITER up,lo;
	bool hasUp,hasLo;
	int phase;
	std::set<std::string> seen;
};

// path as given by the C library (absolute "ov:/a/b", or relative to the overlay's cwd) -> "/a/b"
std::string resolve( const char *path ){
	std::string p=path;
	if( p.compare( 0,3,"ov:" )==0 ) p.erase( 0,3 );
	else if( p.empty() || p[0]!='/' ) p=g_cwd+p;

	std::string out;
	size_t i=0;
	while( i<p.size() ){
		size_t j=p.find( '/',i );
		if( j==std::string::npos ) j=p.size();
		std::string seg=p.substr( i,j-i );
		if( seg==".." ){
			size_t k=out.find_last_of( '/' );
			out.erase( k==std::string::npos ? 0 : k );
		}else if( !seg.empty() && seg!="." ){
			out+="/"+seg;
		}
		i=j+1;
	}
	return out.empty() ? "/" : out;
}

std::string upperPath( const std::string &rel ){ return g_upperRoot+rel; }
std::string lowerPath( const std::string &rel ){ return "romfs:"+rel; }

void commit(){
	if( g_commit ) fsdevCommitDevice( "save" );
}

bool isDir( const devoptab_t *d,struct _reent *r,const std::string &path ){
	struct stat st;
	return d->stat_r( r,path.c_str(),&st )==0 && S_ISDIR( st.st_mode );
}

// create, in the upper layer, the directories above rel that so far exist only in the lower one
void ensureParents( struct _reent *r,const std::string &rel ){
	for( size_t i=rel.find( '/',1 );i!=std::string::npos;i=rel.find( '/',i+1 ) ){
		std::string dir=rel.substr( 0,i );
		if( !isDir( g_upper,r,upperPath( dir ) ) ) CALL( g_upper,mkdir_r,r,upperPath( dir ).c_str(),0777 );
	}
}

bool copyUp( struct _reent *r,const std::string &rel ){
	size_t sz=g_lower->structSize>g_upper->structSize ? g_lower->structSize : g_upper->structSize;
	char *in=(char*)malloc( sz ),*out=(char*)malloc( sz ),*buf=(char*)malloc( 65536 );
	bool ok=false;
	if( in && out && buf ){
		ensureParents( r,rel );
		if( CALL( g_lower,open_r,r,in,lowerPath( rel ).c_str(),O_RDONLY,0 )>=0 ){
			if( CALL( g_upper,open_r,r,out,upperPath( rel ).c_str(),O_WRONLY|O_CREAT|O_TRUNC,0666 )>=0 ){
				ok=true;
				for( ;; ){
					ssize_t n=CALL( g_lower,read_r,r,in,buf,65536 );
					if( n<=0 ){ ok=(n==0); break; }
					if( CALL( g_upper,write_r,r,out,buf,n )!=n ){ ok=false; break; }
				}
				CALL( g_upper,close_r,r,out );
			}
			CALL( g_lower,close_r,r,in );
		}
	}
	free( in ); free( out ); free( buf );
	return ok;
}

int ov_open( struct _reent *r,void *fs,const char *path,int flags,int mode ){
	OvFile *f=(OvFile*)fs;
	std::string rel=resolve( path );
	std::string up=upperPath( rel ),lo=lowerPath( rel );
	f->wrote=0;

	if( !(flags&(O_WRONLY|O_RDWR|O_APPEND|O_CREAT|O_TRUNC)) ){
		int rc=CALL( g_upper,open_r,r,f->inner,up.c_str(),flags,mode );
		if( rc>=0 ){ f->dev=g_upper;return rc; }
		rc=CALL( g_lower,open_r,r,f->inner,lo.c_str(),flags,mode );
		if( rc>=0 ){ f->dev=g_lower;return rc; }
		fprintf( stderr,"[ov] open failed: %s (errno %d)\n",rel.c_str(),r->_errno );
		return -1;
	}

	struct stat st;
	if( !(flags&O_TRUNC) && CALL( g_upper,stat_r,r,up.c_str(),&st )!=0 &&
		CALL( g_lower,stat_r,r,lo.c_str(),&st )==0 && S_ISREG( st.st_mode ) ){
		if( !copyUp( r,rel ) ){ r->_errno=EIO;return -1; }
	}
	ensureParents( r,rel );
	int rc=CALL( g_upper,open_r,r,f->inner,up.c_str(),flags,mode );
	if( rc>=0 ){ f->dev=g_upper;f->wrote=1; }
	return rc;
}

int ov_close( struct _reent *r,void *fd ){
	OvFile *f=(OvFile*)fd;
	int rc=CALL( f->dev,close_r,r,f->inner );
	if( f->wrote ) commit();
	return rc;
}

ssize_t ov_write( struct _reent *r,void *fd,const char *p,size_t n ){
	OvFile *f=(OvFile*)fd;return CALL( f->dev,write_r,r,f->inner,p,n );
}
ssize_t ov_read( struct _reent *r,void *fd,char *p,size_t n ){
	OvFile *f=(OvFile*)fd;return CALL( f->dev,read_r,r,f->inner,p,n );
}
off_t ov_seek( struct _reent *r,void *fd,off_t pos,int dir ){
	OvFile *f=(OvFile*)fd;return CALL( f->dev,seek_r,r,f->inner,pos,dir );
}
int ov_fstat( struct _reent *r,void *fd,struct stat *st ){
	OvFile *f=(OvFile*)fd;return CALL( f->dev,fstat_r,r,f->inner,st );
}
int ov_ftruncate( struct _reent *r,void *fd,off_t len ){
	OvFile *f=(OvFile*)fd;return CALL( f->dev,ftruncate_r,r,f->inner,len );
}
int ov_fsync( struct _reent *r,void *fd ){
	OvFile *f=(OvFile*)fd;
	int rc=f->dev->fsync_r ? CALL( f->dev,fsync_r,r,f->inner ) : 0;
	if( f->wrote ) commit();
	return rc;
}

int ov_stat( struct _reent *r,const char *file,struct stat *st ){
	std::string rel=resolve( file );
	if( CALL( g_upper,stat_r,r,upperPath( rel ).c_str(),st )==0 ) return 0;
	return CALL( g_lower,stat_r,r,lowerPath( rel ).c_str(),st );
}

int ov_unlink( struct _reent *r,const char *name ){
	int rc=CALL( g_upper,unlink_r,r,upperPath( resolve( name ) ).c_str() );
	if( rc==0 ) commit();
	return rc;
}

int ov_chdir( struct _reent *r,const char *name ){
	std::string rel=resolve( name );
	if( !isDir( g_upper,r,upperPath( rel ) ) && !isDir( g_lower,r,lowerPath( rel ) ) ){
		r->_errno=ENOENT;
		return -1;
	}
	g_cwd=rel=="/" ? rel : rel+"/";
	return 0;
}

int ov_rename( struct _reent *r,const char *from,const char *to ){
	std::string a=resolve( from ),b=resolve( to );
	struct stat st;
	if( CALL( g_upper,stat_r,r,upperPath( a ).c_str(),&st )!=0 ){
		// only the read-only layer has it
		r->_errno=CALL( g_lower,stat_r,r,lowerPath( a ).c_str(),&st )==0 ? EROFS : ENOENT;
		return -1;
	}
	ensureParents( r,b );
	int rc=CALL( g_upper,rename_r,r,upperPath( a ).c_str(),upperPath( b ).c_str() );
	if( rc==0 ) commit();
	return rc;
}

int ov_mkdir( struct _reent *r,const char *path,int mode ){
	std::string rel=resolve( path );
	struct stat st;
	if( CALL( g_upper,stat_r,r,upperPath( rel ).c_str(),&st )==0 ||
		CALL( g_lower,stat_r,r,lowerPath( rel ).c_str(),&st )==0 ){
		r->_errno=EEXIST;
		return -1;
	}
	ensureParents( r,rel );
	int rc=CALL( g_upper,mkdir_r,r,upperPath( rel ).c_str(),mode );
	if( rc==0 ) commit();
	return rc;
}

int ov_rmdir( struct _reent *r,const char *name ){
	int rc=CALL( g_upper,rmdir_r,r,upperPath( resolve( name ) ).c_str() );
	if( rc==0 ) commit();
	return rc;
}

int ov_statvfs( struct _reent *r,const char *path,struct statvfs *buf ){
	return CALL( g_upper,statvfs_r,r,upperPath( resolve( path ) ).c_str(),buf );
}

OvDir *dirOf( DIR_ITER *it ){ return *(OvDir**)it->dirStruct; }

DIR_ITER *ov_diropen( struct _reent *r,DIR_ITER *it,const char *path ){
	std::string rel=resolve( path );
	OvDir *d=new OvDir();
	d->up.dirStruct=malloc( g_upper->dirStateSize );
	d->lo.dirStruct=malloc( g_lower->dirStateSize );
	d->hasUp=d->up.dirStruct && CALL( g_upper,diropen_r,r,&d->up,upperPath( rel ).c_str() );
	d->hasLo=d->lo.dirStruct && CALL( g_lower,diropen_r,r,&d->lo,lowerPath( rel ).c_str() );
	d->phase=0;
	if( !d->hasUp && !d->hasLo ){
		free( d->up.dirStruct );free( d->lo.dirStruct );delete d;
		r->_errno=ENOENT;
		return 0;
	}
	*(OvDir**)it->dirStruct=d;
	return it;
}

int ov_dirreset( struct _reent *r,DIR_ITER *it ){
	OvDir *d=dirOf( it );
	d->phase=0;d->seen.clear();
	if( d->hasUp ) CALL( g_upper,dirreset_r,r,&d->up );
	if( d->hasLo ) CALL( g_lower,dirreset_r,r,&d->lo );
	return 0;
}

int ov_dirnext( struct _reent *r,DIR_ITER *it,char *filename,struct stat *st ){
	OvDir *d=dirOf( it );
	for( ;; ){
		if( d->phase==0 ){
			if( d->hasUp && CALL( g_upper,dirnext_r,r,&d->up,filename,st )==0 ){
				d->seen.insert( filename );
				return 0;
			}
			d->phase=1;
		}else{
			if( !d->hasLo || CALL( g_lower,dirnext_r,r,&d->lo,filename,st )!=0 ){
				r->_errno=ENOENT;
				return -1;
			}
			if( !d->seen.count( filename ) ) return 0;
		}
	}
}

int ov_dirclose( struct _reent *r,DIR_ITER *it ){
	OvDir *d=dirOf( it );
	if( d->hasUp ) CALL( g_upper,dirclose_r,r,&d->up );
	if( d->hasLo ) CALL( g_lower,dirclose_r,r,&d->lo );
	free( d->up.dirStruct );free( d->lo.dirStruct );
	delete d;
	return 0;
}

devoptab_t g_dev;

}

void nxOverlayCommit(){
	commit();
}

const char *nxOverlayStatus(){
	return g_status.c_str();
}

bool nxOverlayInit( const char *fallbackDir ){
	// romfs: from the NSP's data partition (or from an NRO's own, which has none for this game)
	char buf[160];
	Result rr=0;
	// NB: GetDeviceOpTab wants "name:"; a bare name is a relative path and yields the default device
	if( !GetDeviceOpTab( "romfs:" ) ) rr=romfsInit();
	if( !GetDeviceOpTab( "romfs:" ) ){
		snprintf( buf,sizeof(buf),"no romfs (romfsInit rc=0x%x): running from the SD card",(unsigned)rr );
		g_status=buf;
		return false;
	}
	if( DIR *d=opendir( "romfs:/Data" ) ) closedir( d );
	else{
		g_status="romfs has no Data directory: running from the SD card";
		return false;
	}

	const char *upperName="save";
	u64 tid=0;
	svcGetInfo( &tid,InfoType_ProgramId,CUR_PROCESS_HANDLE,0 );
	Result sr=fsdevMountDeviceSaveData( "save",tid );
	snprintf( buf,sizeof(buf),"romfs ok, tid=%016llx, save mount rc=0x%x",(unsigned long long)tid,(unsigned)sr );
	g_status=buf;
	if( R_SUCCEEDED( sr ) ){
		g_upperRoot="save:";
		g_commit=true;
	}else{
		// no save data (title without NACP save size, or an NRO that embeds a romfs): use the SD card
		upperName="sdmc";
		g_upperRoot=fallbackDir;
		while( !g_upperRoot.empty() && g_upperRoot.back()=='/' ) g_upperRoot.pop_back();
		mkdir( g_upperRoot.c_str(),0777 );
	}

	g_upper=GetDeviceOpTab( (std::string( upperName )+":").c_str() );
	g_lower=GetDeviceOpTab( "romfs:" );
	if( !g_upper || !g_lower || g_upper==g_lower ){
		g_status+=" -- device lookup failed";
		return false;
	}

	size_t fsz=g_upper->structSize>g_lower->structSize ? g_upper->structSize : g_lower->structSize;
	g_dev.name="ov";
	g_dev.structSize=offsetof( OvFile,inner )+fsz;
	g_dev.open_r=ov_open;
	g_dev.close_r=ov_close;
	g_dev.write_r=ov_write;
	g_dev.read_r=ov_read;
	g_dev.seek_r=ov_seek;
	g_dev.fstat_r=ov_fstat;
	g_dev.stat_r=ov_stat;
	g_dev.lstat_r=ov_stat;
	g_dev.unlink_r=ov_unlink;
	g_dev.chdir_r=ov_chdir;
	g_dev.rename_r=ov_rename;
	g_dev.mkdir_r=ov_mkdir;
	g_dev.rmdir_r=ov_rmdir;
	g_dev.dirStateSize=sizeof( OvDir* );
	g_dev.diropen_r=ov_diropen;
	g_dev.dirreset_r=ov_dirreset;
	g_dev.dirnext_r=ov_dirnext;
	g_dev.dirclose_r=ov_dirclose;
	g_dev.statvfs_r=ov_statvfs;
	g_dev.ftruncate_r=ov_ftruncate;
	g_dev.fsync_r=ov_fsync;
	if( AddDevice( &g_dev )<0 ) return false;

	atexit( nxOverlayCommit );
	return chdir( "ov:/" )==0;
}
