
#include "string.h"
#include "../stdutil/stdutil.h"
#include <utf8.h>

#define CHKPOS(x) if( (x)<0 ) RTEX( "parameter must be positive" );
#define CHKOFF(x) if( (x)<=0 ) RTEX( "parameter must be greater than 0" );

BBStr * BBCALL bbString( BBStr *s,bb_int_t n ){
	BBStr *t=d_new BBStr();
	while( n-->0 ) *t+=*s;
	delete s;return t;
}

BBStr * BBCALL bbLeft( BBStr *s,bb_int_t n ){
	CHKPOS( n );
	const char *last=s->data();
	const char *end=last+s->size();
	while( n-->0&&last<end ){
		utf8_int32_t chr;
		last=utf8codepoint( last,&chr );
	}
	n=last-s->data();
	*s=s->substr( 0,n );return s;
}

BBStr * BBCALL bbRight( BBStr *s,bb_int_t n ){
	CHKPOS( n );
	if( n<=0 ){ s->clear();return s; } // Right$( s,0 ) is "" (this used to return the last character)
	const char *begin=s->data();
	const char *p=begin+s->size();
	while( n-->0&&p>begin ){
		do{ --p; }while( p>begin && (*p&0xC0)==0x80 ); // step back one UTF-8 code point
	}
	*s=s->substr( p-begin );return s;
}

BBStr * BBCALL bbReplace( BBStr *s,BBStr *from,BBStr *to ){
	int n=0,from_sz=from->size(),to_sz=to->size();
	while( n<s->size() && (n=s->find( *from,n ))!=std::string::npos ){
		s->replace( n,from_sz,*to );
		n+=to_sz;
	}
	delete from;delete to;return s;
}

bb_int_t BBCALL bbInstr( BBStr *s,BBStr *t,bb_int_t from ){
	CHKOFF( from );
	// `from` and the result count UTF-8 code points, not bytes. Games call this in
	// tight loops, so walk raw bytes (continuation bytes are 10xxxxxx) and let
	// std::string do the searching.
	const unsigned char *p=(const unsigned char*)s->data();
	const size_t size=s->size();
	size_t pos=0;
	for( bb_int_t steps=from-1;steps>0&&pos<size;--steps ){
		++pos;
		while( pos<size&&(p[pos]&0xC0)==0x80 ) ++pos;
	}

	size_t found=pos>size ? std::string::npos : s->find( *t,pos );
	bb_int_t n=0;
	if( found!=std::string::npos ){
		for( size_t i=0;i<found;++i ) if( (p[i]&0xC0)!=0x80 ) ++n;
		++n;
	}
	delete s;delete t;
	return n;
}

BBStr * BBCALL bbMid( BBStr *s,bb_int_t o,bb_int_t n ){
	CHKOFF( o );
	if( n==-1 ) n=s->size();
	utf8_int32_t chr;
	const char *l=s->c_str(),*r=s->c_str()+s->size();
	const char *p=l;while( --o>0&&p<r ) p=utf8codepoint( p,&chr );
	const char *e=p;while( n-->0&&e<r ) e=utf8codepoint( e,&chr );
	*s=s->substr( p-l,e-p );return s;
}

BBStr * BBCALL bbUpper( BBStr *s ){
	utf8upr( s->data() );
	return s;
}

BBStr * BBCALL bbLower( BBStr *s ){
	utf8lwr( s->data() );
	return s;
}

BBStr * BBCALL bbTrim( BBStr *s ){
	int n=0,p=s->size();
	while( n<s->size() && !isgraph( (*s)[n] ) ) ++n;
	while( p>n && !isgraph( (*s)[p-1] ) ) --p;
	*s=s->substr( n,p-n );return s;
}

BBStr * BBCALL bbLSet( BBStr *s,bb_int_t n ){
	CHKPOS(n);
	if( utf8len( s->c_str() )>n ){
		utf8_int32_t chr;
		const char *l=s->c_str(),*r=s->c_str()+s->size()-1;
		const char *p=l;while( n-->0&&p<r ) p=utf8codepoint( p,&chr );
		*s=s->substr( 0,p-l );
	}else{
		while( utf8len( s->c_str() )<n ) *s+=' ';
	}
	return s;
}

BBStr * BBCALL bbRSet( BBStr *s,bb_int_t n ){
	CHKPOS(n);
	if( utf8len( s->c_str() )>n ){
		utf8_int32_t chr;
		const char *l=s->c_str(),*r=s->c_str()+s->size()-1;
		const char *p=r;while( --n>0&&p>l ) p=utf8rcodepoint( p,&chr );
		*s=s->substr( p-l );
	}else{
		while( utf8len( s->c_str() )<n ) *s=' '+*s;
	}
	return s;
}

BBStr * BBCALL bbChr( bb_int_t n ){
	utf8_int8_t b[4];
	const char *e=utf8catcodepoint( b,n,4 );
	return d_new BBStr( b,e-b );
}

BBStr * BBCALL bbHex( bb_int_t n ){
	char buff[12];
	for( int k=7;k>=0;n>>=4,--k ){
		int t=(n&15)+'0';
		buff[k]=t>'9' ? t+='A'-'9'-1 : t;
	}
	buff[8]=0;
	return d_new BBStr( buff );
}

BBStr * BBCALL bbBin( bb_int_t n ){
	char buff[36];
	for( int k=31;k>=0;n>>=1,--k ){
		buff[k]=n&1 ? '1' : '0';
	}
	buff[32]=0;
	return d_new BBStr( buff );
}

bb_int_t BBCALL bbAsc( BBStr *s ){
	int n=-1;
	if( s->size() ) utf8codepoint( s->data(),&n );
	delete s;return n;
}

bb_int_t BBCALL bbLen( BBStr *s ){
	int n=utf8len( s->c_str() );
	delete s;return n;
}

BBMODULE_CREATE( string ){
	return true;
}

BBMODULE_DESTROY( string ){
	return true;
}
