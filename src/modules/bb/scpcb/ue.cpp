// Native replacements for the third-party DLLs of SCP:CB Ultimate Edition Reborn (BlitzToolbox:
// IniController, S2IMap, RapidBson, FreeImage, user32, uemp). They keep the DLLs' function names
// so the game's own .bb sources stay untouched.

#include <bb/blitz/blitz.h>
#include "scpcb.h"
#include "../stdutil/stdutil.h"

#include <json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace{

std::string lower( std::string s ){
	std::transform( s.begin(),s.end(),s.begin(),[]( unsigned char c ){ return (char)tolower( c ); } );
	return s;
}

std::string trim( const std::string &s ){
	size_t a=0,b=s.size();
	while( a<b && isspace( (unsigned char)s[a] ) ) ++a;
	while( b>a && isspace( (unsigned char)s[b-1] ) ) --b;
	return s.substr( a,b-a );
}

std::string takeStr( BBStr *s ){
	std::string r=*s;delete s;return r;
}

// ---------------------------------------------------------------------------
// IniController
// ---------------------------------------------------------------------------
struct Section{
	std::string name;
	std::vector<std::pair<std::string,std::string> > keys;
};

struct Ini{
	std::vector<Section> sections;

	Section *find( const std::string &name,bool create ){
		std::string l=lower( name );
		for( auto &s:sections ) if( lower( s.name )==l ) return &s;
		if( !create ) return 0;
		sections.push_back( Section() );
		sections.back().name=name;
		return &sections.back();
	}
	const std::string *get( const std::string &sec,const std::string &key ){
		Section *s=find( sec,false );
		if( !s ) return 0;
		std::string k=lower( key );
		for( auto &p:s->keys ) if( lower( p.first )==k ) return &p.second;
		return 0;
	}
	void set( const std::string &sec,const std::string &key,const std::string &val ){
		Section *s=find( sec,true );
		std::string k=lower( key );
		for( auto &p:s->keys ) if( lower( p.first )==k ){ p.second=val;return; }
		s->keys.push_back( std::make_pair( key,val ) );
	}
};

bool parseIni( const std::string &path,Ini &ini ){
	std::string p=canonicalpath( path );
	std::ifstream f( p.c_str(),std::ios::binary );
	if( !f ) return false;
	std::string line;
	Section *cur=0;
	bool first=true;
	while( std::getline( f,line ) ){
		if( first && line.size()>=3 && (unsigned char)line[0]==0xEF && (unsigned char)line[1]==0xBB && (unsigned char)line[2]==0xBF ) line.erase( 0,3 );
		first=false;
		std::string t=trim( line );
		if( t.empty() || t[0]==';' ) continue;
		if( t[0]=='[' ){
			size_t e=t.find( ']' );
			if( e==std::string::npos ) continue;
			cur=ini.find( t.substr( 1,e-1 ),true );
			continue;
		}
		size_t eq=t.find( '=' );
		if( eq==std::string::npos || !cur ) continue;
		std::string k=trim( t.substr( 0,eq ) ),v=trim( t.substr( eq+1 ) );
		bool dup=false;
		for( auto &pr:cur->keys ) if( lower( pr.first )==lower( k ) ){ pr.second=v;dup=true;break; }
		if( !dup ) cur->keys.push_back( std::make_pair( k,v ) );
	}
	return true;
}

void writeIni( const std::string &path,Ini &ini ){
	std::string p=canonicalpath( path );
	FILE *f=fopen( p.c_str(),"wb" );
	if( !f ) return;
	for( auto &s:ini.sections ){
		fprintf( f,"[%s]\r\n",s.name.c_str() );
		for( auto &k:s.keys ) fprintf( f,"%s=%s\r\n",k.first.c_str(),k.second.c_str() );
		fprintf( f,"\r\n" );
	}
	fclose( f );
}

std::map<std::string,Ini> ini_buffers;		// explicit buffers (IniWriteBuffer_)
std::map<std::string,Ini> ini_files;		// parsed copies of files on disk

std::string pathKey( const std::string &p ){ return lower( canonicalpath( p ) ); }

Ini *fileIni( const std::string &path ){
	std::string k=pathKey( path );
	auto it=ini_files.find( k );
	if( it!=ini_files.end() ) return &it->second;
	Ini ini;
	parseIni( path,ini );
	return &(ini_files[k]=ini);
}

Ini *bufferIni( const std::string &path ){
	auto it=ini_buffers.find( pathKey( path ) );
	return it==ini_buffers.end() ? 0 : &it->second;
}

}

extern "C"{

void BBCALL bbIniClearBuffer( BBStr *path ){
	ini_buffers.erase( pathKey( takeStr( path ) ) );
}

void BBCALL bbIniWriteBuffer_( BBStr *path,bb_int_t clear ){
	std::string p=takeStr( path );
	std::string k=pathKey( p );
	if( clear ) ini_buffers.erase( k );
	Ini ini;
	if( parseIni( p,ini ) ){
		Ini &b=ini_buffers[k];
		for( auto &s:ini.sections ) for( auto &kv:s.keys ) b.set( s.name,kv.first,kv.second );
	}
}

BBStr * BBCALL bbIniGetString_( BBStr *path,BBStr *section,BBStr *key,BBStr *def,bb_int_t allow ){
	std::string p=takeStr( path ),s=takeStr( section ),k=takeStr( key ),d=takeStr( def );
	Ini *ini=allow ? bufferIni( p ) : 0;
	if( !ini ) ini=fileIni( p );
	const std::string *v=ini->get( s,k );
	return new BBStr( v ? *v : d );
}

bb_int_t BBCALL bbIniGetInt_( BBStr *path,BBStr *section,BBStr *key,bb_int_t def,bb_int_t allow ){
	std::string p=takeStr( path ),s=takeStr( section ),k=takeStr( key );
	Ini *ini=allow ? bufferIni( p ) : 0;
	if( !ini ) ini=fileIni( p );
	const std::string *v=ini->get( s,k );
	return v ? atoi( v->c_str() ) : def;
}

bb_float_t BBCALL bbIniGetFloat_( BBStr *path,BBStr *section,BBStr *key,bb_float_t def,bb_int_t allow ){
	std::string p=takeStr( path ),s=takeStr( section ),k=takeStr( key );
	Ini *ini=allow ? bufferIni( p ) : 0;
	if( !ini ) ini=fileIni( p );
	const std::string *v=ini->get( s,k );
	return v ? (bb_float_t)atof( v->c_str() ) : def;
}

BBStr * BBCALL bbIniGetBufferString_( BBStr *path,BBStr *section,BBStr *key,BBStr *def ){
	std::string p=takeStr( path ),s=takeStr( section ),k=takeStr( key ),d=takeStr( def );
	Ini *ini=bufferIni( p );
	const std::string *v=ini ? ini->get( s,k ) : 0;
	return new BBStr( v ? *v : d );
}

static void iniWrite( const std::string &p,const std::string &s,const std::string &k,const std::string &v,bb_int_t update ){
	Ini *f=fileIni( p );
	f->set( s,k,v );
	writeIni( p,*f );
	if( update ){
		if( Ini *b=bufferIni( p ) ) b->set( s,k,v );
	}
}

void BBCALL bbIniWriteString_( BBStr *path,BBStr *section,BBStr *key,BBStr *value,bb_int_t update ){
	std::string p=takeStr( path ),s=takeStr( section ),k=takeStr( key ),v=takeStr( value );
	iniWrite( p,s,k,v,update );
}

void BBCALL bbIniWriteInt_( BBStr *path,BBStr *section,BBStr *key,bb_int_t value,bb_int_t update ){
	std::string p=takeStr( path ),s=takeStr( section ),k=takeStr( key );
	iniWrite( p,s,k,std::to_string( (long long)value ),update );
}

void BBCALL bbIniWriteFloat_( BBStr *path,BBStr *section,BBStr *key,bb_float_t value,bb_int_t update ){
	std::string p=takeStr( path ),s=takeStr( section ),k=takeStr( key );
	char tmp[64];snprintf( tmp,sizeof tmp,"%g",(double)value );
	iniWrite( p,s,k,tmp,update );
}

bb_int_t BBCALL bbIniSectionExist_( BBStr *path,BBStr *section,bb_int_t allow ){
	std::string p=takeStr( path ),s=takeStr( section );
	Ini *ini=allow ? bufferIni( p ) : 0;
	if( !ini ) ini=fileIni( p );
	return ini->find( s,false )!=0;
}

bb_int_t BBCALL bbIniKeyExist_( BBStr *path,BBStr *section,BBStr *key,bb_int_t allow ){
	std::string p=takeStr( path ),s=takeStr( section ),k=takeStr( key );
	Ini *ini=allow ? bufferIni( p ) : 0;
	if( !ini ) ini=fileIni( p );
	return ini->get( s,k )!=0;
}

// ---------------------------------------------------------------------------
// S2IMap: string -> int hash map
// ---------------------------------------------------------------------------
typedef std::unordered_map<std::string,bb_int_t> S2IMap;

bb_int_t BBCALL bbCreateS2IMap(){ return (bb_int_t)(intptr_t)new S2IMap(); }
bb_int_t BBCALL bbS2IMapSize( bb_int_t m ){ return m ? (bb_int_t)((S2IMap*)(intptr_t)m)->size() : 0; }
void BBCALL bbS2IMapErase( bb_int_t m,BBStr *k ){ std::string s=takeStr( k );if( m ) ((S2IMap*)(intptr_t)m)->erase( s ); }
void BBCALL bbS2IMapSet( bb_int_t m,BBStr *k,bb_int_t v ){ std::string s=takeStr( k );if( m ) (*(S2IMap*)(intptr_t)m)[s]=v; }
bb_int_t BBCALL bbS2IMapGet( bb_int_t m,BBStr *k ){
	std::string s=takeStr( k );
	if( !m ) return 0;
	S2IMap *map=(S2IMap*)(intptr_t)m;
	auto it=map->find( s );
	return it==map->end() ? 0 : it->second;
}
bb_int_t BBCALL bbS2IMapContains( bb_int_t m,BBStr *k ){
	std::string s=takeStr( k );
	return m && ((S2IMap*)(intptr_t)m)->count( s );
}
void BBCALL bbClearS2IMap( bb_int_t m ){ if( m ) ((S2IMap*)(intptr_t)m)->clear(); }
void BBCALL bbDestroyS2IMap( bb_int_t m ){ delete (S2IMap*)(intptr_t)m; }

// ---------------------------------------------------------------------------
// RapidBson: JSON documents. A "value" is a pointer into a document (0 = missing).
// ---------------------------------------------------------------------------
typedef nlohmann::json Json;

static Json *J( bb_int_t v ){ return (Json*)(intptr_t)v; }

// JSONC: drop // and /* */ comments and trailing commas outside strings
static std::string stripJsonc( const std::string &t ){
	std::string o;o.reserve( t.size() );
	bool str=false;
	for( size_t i=0;i<t.size();++i ){
		char c=t[i];
		if( str ){
			o+=c;
			if( c==92 && i+1<t.size() ) o+=t[++i];
			else if( c=='"' ) str=false;
			continue;
		}
		if( c=='"' ){ str=true;o+=c;continue; }
		if( c=='/' && i+1<t.size() && t[i+1]=='/' ){ while( i<t.size() && t[i]!=10 ) ++i;o+=(char)10;continue; }
		if( c=='/' && i+1<t.size() && t[i+1]=='*' ){ i+=2;while( i+1<t.size() && !(t[i]=='*' && t[i+1]=='/') ) ++i;++i;continue; }
		if( c==',' ){
			size_t j=i+1;
			while( j<t.size() && isspace( (unsigned char)t[j] ) ) ++j;
			if( j<t.size() && (t[j]=='}' || t[j]==']') ) continue;
		}
		o+=c;
	}
	return o;
}

static bb_int_t jsonParse( const std::string &text ){
	Json *doc=new Json( Json::parse( stripJsonc( text ),nullptr,false ) );
	return (bb_int_t)(intptr_t)doc;
}

bb_int_t BBCALL bbJsonParseFromString( BBStr *s ){ return jsonParse( takeStr( s ) ); }

bb_int_t BBCALL bbJsonParseFromFile( BBStr *path ){
	std::string p=canonicalpath( takeStr( path ) );
	std::ifstream f( p.c_str(),std::ios::binary );
	if( !f ) return jsonParse( "" );
	std::stringstream ss;ss<<f.rdbuf();
	std::string t=ss.str();
	if( t.size()>=3 && (unsigned char)t[0]==0xEF && (unsigned char)t[1]==0xBB && (unsigned char)t[2]==0xBF ) t.erase( 0,3 );
	return jsonParse( t );
}

bb_int_t BBCALL bbJsonHasParseError( bb_int_t d ){ return !d || J( d )->is_discarded(); }
void BBCALL bbJsonFreeDocument( bb_int_t d ){ delete J( d ); }

bb_int_t BBCALL bbJsonGetValue( bb_int_t o,BBStr *name ){
	std::string n=takeStr( name );
	if( !o ) return 0;
	Json *j=J( o );
	if( !j->is_object() ) return 0;
	auto it=j->find( n );
	return it==j->end() ? 0 : (bb_int_t)(intptr_t)&*it;
}

bb_int_t BBCALL bbJsonIsString( bb_int_t v ){ return v && J( v )->is_string(); }
bb_int_t BBCALL bbJsonIsFloat( bb_int_t v ){ return v && J( v )->is_number_float(); }
bb_int_t BBCALL bbJsonIsArray( bb_int_t v ){ return v && J( v )->is_array(); }
bb_int_t BBCALL bbJsonIsNull( bb_int_t v ){ return !v || J( v )->is_null(); }

BBStr * BBCALL bbJsonGetString( bb_int_t v ){
	if( v && J( v )->is_string() ) return new BBStr( J( v )->get<std::string>() );
	return new BBStr();
}

bb_int_t BBCALL bbJsonGetInt( bb_int_t v ){
	if( !v ) return 0;
	Json *j=J( v );
	if( j->is_number() ) return (bb_int_t)j->get<double>();
	if( j->is_boolean() ) return j->get<bool>();
	return 0;
}

bb_float_t BBCALL bbJsonGetFloat( bb_int_t v ){
	if( v && J( v )->is_number() ) return (bb_float_t)J( v )->get<double>();
	return 0;
}

bb_int_t BBCALL bbJsonGetBool( bb_int_t v ){
	if( !v ) return 0;
	Json *j=J( v );
	if( j->is_boolean() ) return j->get<bool>();
	if( j->is_number() ) return j->get<double>()!=0;
	return 0;
}

bb_int_t BBCALL bbJsonGetArray( bb_int_t v ){ return v && J( v )->is_array() ? v : 0; }
bb_int_t BBCALL bbJsonGetArraySize( bb_int_t a ){ return a && J( a )->is_array() ? (bb_int_t)J( a )->size() : 0; }
bb_int_t BBCALL bbJsonGetArrayValue( bb_int_t a,bb_int_t i ){
	if( !a || !J( a )->is_array() || i<0 || (size_t)i>=J( a )->size() ) return 0;
	return (bb_int_t)(intptr_t)&(*J( a ))[(size_t)i];
}

// ---------------------------------------------------------------------------
// Miscellany: downloads, FreeImage, user32, uemp memory access
// ---------------------------------------------------------------------------
BBStr * BBCALL bbFindNextDirectory( BBStr *path,BBStr *dir,BBStr *def ){
	delete path;delete dir;
	return def;
}
void BBCALL bbDownloadFileThread( BBStr *url,BBStr *file ){ delete url;delete file; }
bb_int_t BBCALL bbGetDownloadFileThreadSize(){ return 0; }

bb_int_t BBCALL bbFI_Load( bb_int_t type,BBStr *file,bb_int_t mode ){ delete file;return 0; }
bb_int_t BBCALL bbFI_Save( bb_int_t type,bb_int_t bmp,BBStr *file,bb_int_t flags ){ delete file;return 0; }
bb_int_t BBCALL bbFI_Unload( bb_int_t bmp ){ return 0; }
bb_int_t BBCALL bbFI_GetFIFFromFilename( BBStr *file ){ delete file;return 0; }
bb_int_t BBCALL bbFI_Rescale( bb_int_t bmp,bb_int_t w,bb_int_t h,bb_int_t filter ){ return 0; }


bb_int_t BBCALL bbapi_GetForegroundWindow(){ return 0; }

bb_int_t BBCALL bbMemory_PeekInt( bb_int_t p ){ return p ? *(int*)(intptr_t)p : 0; }
void BBCALL bbMemory_PokeInt( bb_int_t p,bb_int_t v ){ if( p ) *(int*)(intptr_t)p=(int)v; }

}
