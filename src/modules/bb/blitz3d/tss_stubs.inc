
// ---- TSS render pipeline stubs ----
BBLIB void BBCALL bbTssBrushMaterial( bb_int_t a0, bb_float_t a1, bb_float_t a2 ){  }
BBLIB void BBCALL bbTssBrushEffect( bb_int_t a0, bb_int_t a1 ){  }
BBLIB void BBCALL bbTssEntityEffect( bb_int_t a0, bb_int_t a1 ){  }
BBLIB void BBCALL bbTssMaskEntity( bb_int_t a0, bb_int_t a1 ){  }
BBLIB void BBCALL bbTssCaptureEntity( bb_int_t a0 ){  }
BBLIB void BBCALL bbTssRenderEntity( bb_int_t a0, bb_float_t a1, bb_float_t a2 ){  }
BBLIB void BBCALL bbTssBeginRender( bb_float_t a0, bb_int_t a1 ){  }
BBLIB void BBCALL bbTssEndRender(  ){  }
BBLIB void BBCALL bbTssCameraReverseZ( bb_int_t a0, bb_int_t a1 ){  }
BBLIB void BBCALL bbTssCameraDepthBias( bb_int_t a0, bb_float_t a1, bb_float_t a2 ){  }
BBLIB void BBCALL bbTssCameraColorWrite( bb_int_t a0, bb_int_t a1 ){  }
BBLIB void BBCALL bbTssReloadEffect( bb_int_t a0, BBStr * a1, BBStr * a2 ){ delete a1;delete a2; }
BBLIB void BBCALL bbTssEffectTechnique( bb_int_t a0, BBStr * a1 ){ delete a1; }
BBLIB void BBCALL bbTssEffectFloat( bb_int_t a0, BBStr * a1, bb_float_t a2 ){ delete a1; }
BBLIB void BBCALL bbTssEffectInt( bb_int_t a0, BBStr * a1, bb_int_t a2 ){ delete a1; }
BBLIB void BBCALL bbTssEffectVector( bb_int_t a0, BBStr * a1, bb_float_t a2, bb_float_t a3, bb_float_t a4 ){ delete a1; }
BBLIB void BBCALL bbTssEffectMatrix( bb_int_t a0, BBStr * a1, bb_int_t a2 ){ delete a1; }
BBLIB void BBCALL bbTssEffectTexture( bb_int_t a0, BBStr * a1, bb_int_t a2 ){ delete a1; }
BBLIB void BBCALL bbTssTextureAnisotropic( bb_int_t a0 ){  }
BBLIB void BBCALL bbTssTextureDivisor( bb_int_t a0 ){  }
BBLIB void BBCALL bbTssTexturePersistentCaching( bb_int_t a0 ){  }
BBLIB void BBCALL bbTssClearUnusedTextures(  ){  }
BBLIB void BBCALL bbTssHardwareSkinning( bb_int_t a0 ){  }
BBLIB void BBCALL bbTssBatching( bb_int_t a0 ){  }
BBLIB void BBCALL bbTssGraphicsLevel( bb_int_t a0 ){  }
BBLIB bb_int_t BBCALL bbTssLoadEffect( BBStr * a0, BBStr * a1 ){ delete a0;delete a1;return 1; }
BBLIB bb_int_t BBCALL bbTssCameraMatrix( bb_int_t a0, bb_int_t a1, bb_float_t a2 ){ return 0; }
BBLIB BBStr * BBCALL bbTssGetEffectError(  ){ return new BBStr(); }
BBLIB BBStr * BBCALL bbTssGetGraphicsLevel(  ){ return new BBStr( "11.0" ); }
BBLIB void BBCALL bbUpdateTB( bb_int_t mesh ){}
