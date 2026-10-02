#include "../stdutil/stdutil.h"
#include "package.h"
#include <iostream>
#include <stdlib.h>
#include <filesystem>

// system() goes through cmd.exe on Windows, which wants the whole command wrapped in quotes
static int run( const std::string &cmd ){
#ifdef WIN32
	return system( ("\""+cmd+"\"").c_str() );
#else
	return system( cmd.c_str() );
#endif
}

#define RUN( args ) if( run( std::string(args) )!=0 ) { std::cerr<<"error on "<<__FILE__<<":"<<__LINE__<<std::endl;exit(1); }

void createNRO( const std::string &out,const std::string &home,const std::string &devkitpro,const BundleInfo &bundle,const Target &target,const std::string &elfPath ){
	std::string dir=filenamepath( out );
	std::string base=filenamefile( out );
	base=base.substr( 0,base.size()-4 );
	std::string nacpPath=dir+"/"+base+".nacp";

	std::string tmpdir=dir+"/"+base+"-tmp";
	std::string romDir=tmpdir+"/romfs";

	std::filesystem::create_directories( romDir );
	bundleFiles( bundle,romDir );

	// must be a jpg? 256x256
	std::string icon=home+"/cfg/bbexe.jpg";
	if( !std::filesystem::exists( icon ) ) icon=devkitpro+"/libnx/default_icon.jpg";

#ifdef WIN32
	const std::string exe=".exe";
#else
	const std::string exe="";
#endif

	RUN( "\""+devkitpro+"/tools/bin/nacptool"+exe+"\" --create \""+bundle.appName+"\" \"Unspecified Author\" \"1.0.0\" \""+nacpPath+"\"" );
	RUN( "\""+devkitpro+"/tools/bin/elf2nro"+exe+"\" \""+elfPath+"\" \""+out+"\" --icon=\""+icon+"\" --nacp=\""+nacpPath+"\" --romfsdir=\""+romDir+"\"" );

	// BB_KEEP_ELF=<path> keeps the linked ELF so crash addresses can be looked up with nm/addr2line
	if( const char *keep=getenv( "BB_KEEP_ELF" ) ) std::filesystem::copy_file( elfPath,keep,std::filesystem::copy_options::overwrite_existing );
	remove( elfPath.c_str() );
	remove( nacpPath.c_str() );
}
