#include "../stdutil/stdutil.h"
#include "graphics.gl.h"

#include <vector>

GLuint _bbGLCompileShader( GLenum type,const std::string &name,const std::string &source ){
#ifdef BB_DESKTOP
	static char version[]         = "#version 330    \n";
#else
	static char version[]         = "#version 300 es \n";
#endif
	static char define_vertex[]   = "#define VERTEX  \n";
	static char define_fragment[] = "#define FRAGMENT\n";

	const char *sources[] = {
		version,
		type==GL_VERTEX_SHADER?define_vertex:define_fragment,
		source.c_str()
	};
	int source_lens[] = { 17,17,(int)source.size() };

	GLuint shader=GL( glCreateShader( type ) );
	GL( glShaderSource( shader,3,sources,source_lens ) );
	GL( glCompileShader( shader ) );

	GLint status;
	GL( glGetShaderiv(shader, GL_COMPILE_STATUS, &status) );
	if( status==0 ){
		LOGD( "Couldn't compile shader: %s\n",name.c_str() );
		GLint logLength;
		GL( glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength) );
		if( logLength>0 ){
			GLchar *log = (GLchar*)malloc(logLength);
			GL( glGetShaderInfoLog(shader, logLength, &logLength, log) );
			if( log[0]!=0 ){
				LOGD( "Shader log: %s\n",log );
			}
			free( log );
		}
		GL( glDeleteShader(shader) );

		shader=0;
	}
	return shader;
}

GLuint _bbGLCompileProgram( const std::string &name,const std::string &src ){
	GLuint vert=_bbGLCompileShader( GL_VERTEX_SHADER,name,src );
	GLuint frag=_bbGLCompileShader( GL_FRAGMENT_SHADER,name,src );

	if( vert==0||frag==0 ){
		LOGD( "%s","Failed to compile shader" );
		exit(1);
	}
	GLuint program=GL( glCreateProgram() );

	GL( glAttachShader( program,vert ) );
	GL( glAttachShader( program,frag ) );

	int link_status=0;
	GL( glLinkProgram( program ) );
	GL( glGetProgramiv( program,GL_LINK_STATUS,&link_status ) );
	if( !link_status ){
		LOGD( "%s","Failed to link linker" );

		GLint maxLength = 0;
		GL( glGetProgramiv( program,GL_INFO_LOG_LENGTH,&maxLength ) );

		GLchar *log=(GLchar*)malloc( maxLength );
		GL( glGetProgramInfoLog( program,maxLength,&maxLength,&log[0] ) );
		if( log[0]!=0 ){
			LOGD( "%s",log );
		}
		free( log );

		exit(1);
	}

	GL( glDeleteShader( vert ) );
	GL( glDeleteShader( frag ) );

	return program;
}

static
const char * GlErrorString( GLenum error ) {
	switch ( error ) {
  case GL_NO_ERROR:                      return "GL_NO_ERROR";
  case GL_INVALID_ENUM:                  return "GL_INVALID_ENUM";
  case GL_INVALID_VALUE:                 return "GL_INVALID_VALUE";
  case GL_INVALID_OPERATION:             return "GL_INVALID_OPERATION";
  case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
  case GL_OUT_OF_MEMORY:                 return "GL_OUT_OF_MEMORY";
  default: return "unknown";
	}
}

const char * bbGLFramebufferStatusString( GLenum status ) {
	switch ( status ) {
	case GL_FRAMEBUFFER_COMPLETE:                      return "GL_FRAMEBUFFER_COMPLETE";
	case GL_FRAMEBUFFER_UNDEFINED:                     return "GL_FRAMEBUFFER_UNDEFINED";
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:         return "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";
	case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: return "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT";
	case GL_FRAMEBUFFER_UNSUPPORTED:                   return "GL_FRAMEBUFFER_UNSUPPORTED";
	case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:        return "GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE";
#ifdef GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER
	case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:        return "GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER";
#endif
#ifdef GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER
	case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:        return "GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER";
#endif
#ifdef GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS
	case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS:      return "GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS";
#endif
	default:                                           return "unknown";
	}
}

void bbGLGraphicsCheckErrors( const char *file, int line ){
	for( int i=0;i<10;i++ ){
		const GLenum error=glGetError();
		if( error==GL_NO_ERROR ){
			break;
		}
		LOGD( "GL error on %s:%d: %s", file, line, GlErrorString( error ) );
	}
}

GLGraphics::GLGraphics():fb(&res,BBCanvas::CANVAS_TEX_VIDMEM),bb(&res,BBCanvas::CANVAS_TEX_VIDMEM){
	GLint framebuffer=0;
#ifdef BB_IOS
	// ios doesn't supply a default framebuffer
	GL( glGetIntegerv( GL_FRAMEBUFFER_BINDING,(int*)&framebuffer ) );
	LOGD( "framebuffer: %i\n", framebuffer );
#endif

	fb.setFramebuffer( framebuffer,GL_FRONT );
	front_canvas=&fb;

	bb.setFramebuffer( framebuffer,GL_BACK );
	back_canvas=&fb;

	GL( glDisable( GL_DEPTH_TEST ) );
	GL( glEnable( GL_SCISSOR_TEST ) );
}

bool GLGraphics::init(){
	def_font=(BBImageFont*)loadFont( "courier",12,0 );
	if( def_font==0 ){
		def_font=(BBImageFont*)loadFont( "courier new",12,0 );
	}

	return def_font!=0;
}

BBFont *GLGraphics::getDefaultFont()const{
	return def_font;
}

//OBJECTS
BBCanvas *GLGraphics::createCanvas( int width,int height,int flags ){
	GLCanvas *canvas=d_new GLCanvas( &res,width,height,flags );
	canvas_set.insert( canvas );
	return canvas;
}

// Largest texture dimension to keep (0 = no limit). The Switch has far less memory
// than the PC this was designed for; BB_TEXTURE_MAX overrides the default.
static int textureSizeCap(){
	static int cap=-1;
	if( cap<0 ){
#ifdef BB_NX
		cap=512;
#else
		cap=0;
#endif
		if( const char *e=getenv( "BB_TEXTURE_MAX" ) ) cap=atoi( e );
	}
	return cap;
}

// 2x2 box filter on a 4 byte per pixel pixmap
static void halvePixmap( BBPixmap *pm ){
	const int w=pm->width,h=pm->height,nw=w>1 ? w/2 : 1,nh=h>1 ? h/2 : 1;
	unsigned char *out=new unsigned char[(size_t)nw*nh*4];
	for( int y=0;y<nh;y++ ){
		for( int x=0;x<nw;x++ ){
			const int x0=x*2,y0=y*2,x1=x0+1<w ? x0+1 : x0,y1=y0+1<h ? y0+1 : y0;
			for( int c=0;c<4;c++ ){
				int sum=pm->bits[((size_t)y0*w+x0)*4+c]+pm->bits[((size_t)y0*w+x1)*4+c]
					+pm->bits[((size_t)y1*w+x0)*4+c]+pm->bits[((size_t)y1*w+x1)*4+c];
				out[((size_t)y*nw+x)*4+c]=(unsigned char)((sum+2)/4);
			}
		}
	}
	delete[] pm->bits;
	pm->bits=out;
	pm->width=nw;pm->height=nh;
	pm->pitch=nw*4;
}

BBCanvas *GLGraphics::loadCanvas( const std::string &file,int flags ){
	BBPixmap *pixmap=bbLoadPixmap( file );
	if( !pixmap ) return 0;

	pixmap->flipVertically();
	pixmap->swapBytes0and2();

	// 3D textures (as opposed to 2D images) get a size cap and drop their CPU copy
	const bool texture=(flags&(BBCanvas::CANVAS_TEX_RGB|BBCanvas::CANVAS_TEX_ALPHA|BBCanvas::CANVAS_TEX_MASK))!=0;
	if( texture && !(flags&BBCanvas::CANVAS_NONDISPLAY) ){
		// (NONDISPLAY loads are sheets that get cut into frames by pixel size: leave those)
		int cap=textureSizeCap();
		while( cap>0 && (pixmap->width>cap || pixmap->height>cap) && pixmap->width>1 && pixmap->height>1 && pixmap->bpp==4 ){
			halvePixmap( pixmap );
		}
	}

	GLCanvas *canvas=d_new GLCanvas( &res,flags );
	canvas->setPixmap( pixmap );
	canvas_set.insert( canvas );
	if( texture && !(flags&BBCanvas::CANVAS_NONDISPLAY) ) canvas->discardSystemCopy();

	return canvas;
}

BBMovie *GLGraphics::openMovie( const std::string &file,int flags ){
	return 0;
}

BBMODULE_CREATE( graphics_gl ){
	return true;
}

BBMODULE_DESTROY( graphics_gl ){
	return true;
}
